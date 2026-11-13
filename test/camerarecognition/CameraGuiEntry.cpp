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
#include <string>
#include <vector>

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
