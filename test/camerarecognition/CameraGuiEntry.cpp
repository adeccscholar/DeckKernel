// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/**
\file CameraGuiEntry.cpp
\brief Windows GUI entry point forwarding command arguments to the existing app.
\details Does not allocate a console. Legacy self-tests and replay arguments
   are still accepted by the unchanged main entry point.
*/
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <ocidl.h>
#include <initguid.h>
#include <dshow.h>
#include <string>
#include <utility>
#include <vector>

namespace deckkernel::camera {
   /**
   \brief Discover Windows DirectShow devices in the OpenCV DSHOW index order.
   \details Uses FriendlyName from each video-input moniker. This does not
      activate any camera or change its controls.
   \returns Indexed camera names, or an empty list on enumeration failure.
   \throw None.
   */
   std::vector<std::pair<int,std::wstring>> EnumerateCameraDevices() {
      std::vector<std::pair<int,std::wstring>> vecDevices;
      HRESULT const hInitialization=CoInitializeEx(
         nullptr,COINIT_APARTMENTTHREADED);
      if(FAILED(hInitialization) && hInitialization!=RPC_E_CHANGED_MODE)
         return vecDevices;
      bool const boUninitialize=SUCCEEDED(hInitialization);
      ICreateDevEnum* pDeviceEnumerator{};
      HRESULT const hCreated=CoCreateInstance(
         CLSID_SystemDeviceEnum,nullptr,CLSCTX_INPROC_SERVER,
         IID_ICreateDevEnum,reinterpret_cast<void**>(&pDeviceEnumerator));
      if(SUCCEEDED(hCreated) && pDeviceEnumerator) {
         IEnumMoniker* pMonikers{};
         HRESULT const hEnumerated=pDeviceEnumerator->CreateClassEnumerator(
            CLSID_VideoInputDeviceCategory,&pMonikers,0);
         if(hEnumerated==S_OK && pMonikers) {
            IMoniker* pMoniker{};
            ULONG uFetched{};
            while(pMonikers->Next(1,&pMoniker,&uFetched)==S_OK) {
               IPropertyBag* pBag{};
               std::wstring strName=L"Video input";
               if(SUCCEEDED(pMoniker->BindToStorage(nullptr,nullptr,
                  IID_IPropertyBag,reinterpret_cast<void**>(&pBag))) &&
                  pBag) {
                  VARIANT aValue{};
                  VariantInit(&aValue);
                  if(SUCCEEDED(pBag->Read(L"FriendlyName",&aValue,nullptr)) &&
                     aValue.vt==VT_BSTR && aValue.bstrVal)
                     strName=aValue.bstrVal;
                  VariantClear(&aValue);
                  pBag->Release();
               }
               vecDevices.emplace_back(
                  static_cast<int>(vecDevices.size()),std::move(strName));
               pMoniker->Release();
               pMoniker=nullptr;
            }
            pMonikers->Release();
         }
         pDeviceEnumerator->Release();
      }
      if(boUninitialize)CoUninitialize();
      return vecDevices;
   }
}


int main(int argc,char** argv);

/**
\brief Start the graphical camera application without a console window.
\details Converts UTF-16 command arguments to UTF-8 for the existing parser.
\param hInstance Application instance.
\param hPreviousInstance Unused previous instance.
\param szCommandLine Unused, parsed through GetCommandLineW instead.
\param iShowCommand Window show preference.
\returns Process exit status.
\throw None.
*/
int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE hPreviousInstance,
   LPSTR szCommandLine,int iShowCommand) {
   int iCount{};
   LPWSTR* pArgs=CommandLineToArgvW(GetCommandLineW(),&iCount);
   if(!pArgs)return 1;
   std::vector<std::string> vecArgs;
   vecArgs.reserve(static_cast<std::size_t>(iCount));
   for(int iIndex=0;iIndex<iCount;++iIndex) {
      int const iLength=WideCharToMultiByte(CP_UTF8,0,pArgs[iIndex],
         -1,nullptr,0,nullptr,nullptr);
      if(iLength<=0) {
         LocalFree(pArgs);
         return 1;
      }
      std::string strArg(static_cast<std::size_t>(iLength),'\0');
      WideCharToMultiByte(CP_UTF8,0,pArgs[iIndex],-1,
         strArg.data(),iLength,nullptr,nullptr);
      strArg.pop_back();
      vecArgs.push_back(std::move(strArg));
   }
   LocalFree(pArgs);
   std::vector<char*> vecPointers;
   vecPointers.reserve(vecArgs.size());
   for(std::string& strArg:vecArgs)
      vecPointers.push_back(strArg.data());
   return main(static_cast<int>(vecPointers.size()),vecPointers.data());
}
