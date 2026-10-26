// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#include "image_cache.h"
#include <openssl/evp.h>
#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace deckkernel::scryfall {
namespace {
std::filesystem::path ReferencePath(std::filesystem::path const& aRoot,
   std::string_view svUrl) {
   auto const* pBegin=reinterpret_cast<std::uint8_t const*>(svUrl.data());
   std::string const strHash=Sha256({pBegin,svUrl.size()});
   return aRoot/"urls"/strHash.substr(0,2)/(strHash+".ref");
}
std::filesystem::path BlobPath(std::filesystem::path const& aRoot,
   std::string const& strDigest) {
   return aRoot/"blobs"/strDigest.substr(0,2)/(strDigest+".img");
}
bool IsDigest(std::string_view svText) {
   if(svText.size()!=64) return false;
   for(char ch:svText) if(!((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f'))) return false;
   return true;
}
std::vector<std::uint8_t> ReadBytes(std::filesystem::path const& aPath) {
   if(!std::filesystem::is_regular_file(aPath)) return {};
   std::uintmax_t const uSize=std::filesystem::file_size(aPath);
   if(!uSize||uSize>ImageCache::uMaximumBytes) return {};
   std::ifstream isFile(aPath,std::ios::binary);
   if(!isFile) return {};
   std::vector<std::uint8_t> vecData(static_cast<std::size_t>(uSize));
   isFile.read(reinterpret_cast<char*>(vecData.data()),
      static_cast<std::streamsize>(vecData.size()));
   if(!isFile) return {};
   return vecData;
}
void WriteAtomic(std::filesystem::path const& aPath,
   std::span<std::uint8_t const> spBytes) {
   std::filesystem::create_directories(aPath.parent_path());
   auto aTemporary=aPath;
   aTemporary+=L".tmp";
   {
      std::ofstream osFile(aTemporary,std::ios::binary|std::ios::trunc);
      if(!osFile) throw std::runtime_error("image cache temporary file cannot be opened");
      osFile.write(reinterpret_cast<char const*>(spBytes.data()),
         static_cast<std::streamsize>(spBytes.size()));
      osFile.flush();
      if(!osFile) throw std::runtime_error("image cache temporary file cannot be written");
   }
   // Windows cannot rename over an existing destination.
   // The caller verifies content before replacing a hash-addressed file.
   if(std::filesystem::exists(aPath)) std::filesystem::remove(aPath);
   std::filesystem::rename(aTemporary,aPath);
}
void WriteTextAtomic(std::filesystem::path const& aPath,std::string const& strText) {
   auto const* pBytes=reinterpret_cast<std::uint8_t const*>(strText.data());
   WriteAtomic(aPath,{pBytes,strText.size()});
}
}

std::string Sha256(std::span<std::uint8_t const> spBytes) {
   std::array<unsigned char,EVP_MAX_MD_SIZE> arrDigest{};
   unsigned int uLength{};
   if(EVP_Digest(spBytes.data(),spBytes.size(),arrDigest.data(),&uLength,
      EVP_sha256(),nullptr)!=1||uLength!=32)
      throw std::runtime_error("OpenSSL SHA-256 computation failed");
   static constexpr char szHex[]="0123456789abcdef";
   std::string strResult;
   strResult.reserve(64);
   for(unsigned int uIndex=0;uIndex<uLength;++uIndex) {
      strResult.push_back(szHex[arrDigest[uIndex]>>4]);
      strResult.push_back(szHex[arrDigest[uIndex]&15]);
   }
   return strResult;
}
std::optional<std::string> DetectImageType(std::span<std::uint8_t const> spBytes) {
   if(spBytes.size()>=4 && spBytes[0]==0xff && spBytes[1]==0xd8 &&
      spBytes[spBytes.size()-2]==0xff && spBytes.back()==0xd9)
      return "image/jpeg";
   if(spBytes.size()>=8 &&
      spBytes[0]==137 && spBytes[1]==80 && spBytes[2]==78 && spBytes[3]==71 &&
      spBytes[4]==13 && spBytes[5]==10 && spBytes[6]==26 && spBytes[7]==10)
      return "image/png";
   if(spBytes.size()>=12 &&
      spBytes[0]=='R' && spBytes[1]=='I' && spBytes[2]=='F' && spBytes[3]=='F' &&
      spBytes[8]=='W' && spBytes[9]=='E' && spBytes[10]=='B' && spBytes[11]=='P')
      return "image/webp";
   return std::nullopt;
}

ImageCache::ImageCache(std::filesystem::path aRoot):
   m_aRoot(std::move(aRoot)) {
   if(m_aRoot.empty()) throw std::runtime_error("image cache root is empty");
}
std::filesystem::path const& ImageCache::Root() const noexcept {
   return m_aRoot;
}
std::optional<CachedImage> ImageCache::Find(std::string_view svUrl) const {
   std::ifstream isReference(ReferencePath(m_aRoot,svUrl),std::ios::binary);
   if(!isReference) return std::nullopt;
   std::string strDigest,strMime,strSource;
   if(!std::getline(isReference,strDigest) || !std::getline(isReference,strMime) ||
      !std::getline(isReference,strSource) || !IsDigest(strDigest) ||
      strSource!=svUrl) return std::nullopt;
   auto vecBytes=ReadBytes(BlobPath(m_aRoot,strDigest));
   if(vecBytes.empty()||Sha256(vecBytes)!=strDigest) return std::nullopt;
   auto optMime=DetectImageType(vecBytes);
   if(!optMime || *optMime!=strMime) return std::nullopt;
   return CachedImage{std::move(vecBytes),std::move(strDigest),std::move(strMime)};
}
bool ImageCache::RemoveCommitted(std::string_view svUrl,
   std::string_view svExpectedSha256) {
   if(!IsDigest(svExpectedSha256)) return false;
   std::filesystem::path const aReference=ReferencePath(m_aRoot,svUrl);
   std::ifstream isReference(aReference,std::ios::binary);
   if(!isReference) return false;
   std::string strDigest,strMime,strSource;
   if(!std::getline(isReference,strDigest) ||
      !std::getline(isReference,strMime) ||
      !std::getline(isReference,strSource) ||
      strDigest!=svExpectedSha256 || strSource!=svUrl)
      return false;
   isReference.close();

   // A blob may have multiple URL references; build the index only when
   // an existing disk entry actually needs eviction.
   if(!m_bReferenceIndexBuilt) {
      std::unordered_map<std::string,std::size_t> mpCounts;
      std::filesystem::path const aRefs=m_aRoot/"urls";
      if(std::filesystem::is_directory(aRefs)) {
         for(auto const& aEntry:
            std::filesystem::recursive_directory_iterator(aRefs)) {
            if(!aEntry.is_regular_file() || aEntry.path().extension()!=".ref")
               continue;
            std::ifstream isEntry(aEntry.path(),std::ios::binary);
            std::string strEntryDigest;
            if(std::getline(isEntry,strEntryDigest) && IsDigest(strEntryDigest))
               ++mpCounts[strEntryDigest];
         }
      }
      m_mpBlobReferences=std::move(mpCounts);
      m_bReferenceIndexBuilt=true;
   }

   std::error_code aError;
   bool const bRemoved=std::filesystem::remove(aReference,aError);
   if(aError || !bRemoved) return false;
   auto aCount=m_mpBlobReferences.find(strDigest);
   if(aCount!=m_mpBlobReferences.end() && aCount->second>0) {
      if(--aCount->second==0) {
         m_mpBlobReferences.erase(aCount);
         std::error_code aBlobError;
         std::filesystem::remove(BlobPath(m_aRoot,strDigest),aBlobError);
         // Orphan blob cleanup failure is not a data loss: PostgreSQL
         // already committed the original bytes in both schemas.
      }
   }
   return true;
}

CachedImage ImageCache::Store(std::string_view svUrl,
   std::span<std::uint8_t const> spBytes) const {
   if(svUrl.empty()||svUrl.find('\n')!=std::string_view::npos ||
      spBytes.empty()||spBytes.size()>uMaximumBytes)
      throw std::runtime_error("invalid image cache URL or payload size");
   auto optMime=DetectImageType(spBytes);
   if(!optMime) throw std::runtime_error("image cache rejects unsupported image signature");
   std::string const strDigest=Sha256(spBytes);
   auto const aBlob=BlobPath(m_aRoot,strDigest);
   auto const vecStored=ReadBytes(aBlob);
   if(vecStored.empty()||Sha256(vecStored)!=strDigest) WriteAtomic(aBlob,spBytes);
   std::string const strRef=strDigest+"\n"+*optMime+"\n"+std::string(svUrl)+"\n";
   WriteTextAtomic(ReferencePath(m_aRoot,svUrl),strRef);
   return CachedImage{std::vector<std::uint8_t>(spBytes.begin(),spBytes.end()),
      strDigest,*optMime};
}
}
