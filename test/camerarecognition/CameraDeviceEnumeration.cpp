// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/**
\file CameraDeviceEnumeration.cpp
\brief DirectShow camera enumeration without application entry point.
*/
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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

