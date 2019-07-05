/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>

#include <string.h>

#include <windows.h>
#undef _WINSOCKAPI_
#include <winsock2.h>
#include <ws2spi.h>
#include <rpc.h>

using namespace std;

template <typename T, size_t N>
size_t ArrayLength(T (&aArr)[N]) {
  return N;
}

string ToUTF8(const wchar_t* aStr, size_t aLenExcl) {
  int result = WideCharToMultiByte(CP_UTF8, 0, aStr, aLenExcl, nullptr, 0,
                                   nullptr, nullptr);
  if (!result) {
    return string();
  }

  auto str = make_unique<char[]>(result);
  result = WideCharToMultiByte(CP_UTF8, 0, aStr, aLenExcl, str.get(), result,
                               nullptr, nullptr);
  if (!result) {
    return string();
  }

  return string(str.get(), result);
}

string ToUTF8(const wchar_t* aStr) { return ToUTF8(aStr, wcslen(aStr)); }

template <size_t N>
string ToUTF8(const wchar_t (&aStr)[N]) {
  return ToUTF8(aStr, N - 1);
}

extern "C" int wmain(int argc, wchar_t* argv[]) {
  bool useStdout = false;
  if (argc == 2 && !wcscmp(argv[1], L"--")) {
    useStdout = true;
  }

  WSADATA wsaData;
  if (WSAStartup(WINSOCK_VERSION, &wsaData)) {
    cerr << "WSAStartup failed" << endl;
    return 1;
  }

  DWORD size;
  INT errno = 0;
  int result = WSCEnumProtocols(nullptr, nullptr, &size, &errno);
  if (result == SOCKET_ERROR && errno != WSAENOBUFS) {
    cerr << "WSCEnumProtocols length query failed" << endl;
    WSACleanup();
    return 1;
  }

  size_t count = size / sizeof(WSAPROTOCOL_INFOW);
  auto providers = make_unique<WSAPROTOCOL_INFOW[]>(count);
  // Just in case calculation of count rounded down (unlikely, but...)
  size = count * sizeof(WSAPROTOCOL_INFOW);
  result = WSCEnumProtocols(nullptr, providers.get(), &size, &errno);
  if (result == SOCKET_ERROR) {
    cerr << "WSCEnumProtocols failed" << endl;
    WSACleanup();
    return 1;
  }

  ofstream outfile;
  if (!useStdout) {
    outfile.open("lsp.log");
  }

  ostream& o = useStdout ? cout : outfile;

  const size_t kIndentWidth = 4;
  string logIndent(1 * kIndentWidth, ' ');
  string entryIndent(2 * kIndentWidth, ' ');
  string chainIndent(3 * kIndentWidth, ' ');

  o << "BEGIN LOG" << endl << endl;

  for (int i = 0; i < result; ++i) {
    WSAPROTOCOL_INFOW& p = providers[i];

    o << logIndent << "BEGIN ENTRY [" << i << "]: " << endl;
    o << entryIndent << "Description: " << ToUTF8(p.szProtocol) << endl;

    wchar_t provPath[MAX_PATH + 1] = {0};
    INT provPathLen = MAX_PATH;
    int status =
        WSCGetProviderPath(&p.ProviderId, provPath, &provPathLen, &errno);
    if (!status) {
      o << entryIndent << "DLL: " << ToUTF8(provPath, provPathLen) << endl;
    }

    switch (p.ProtocolChain.ChainLen) {
      case 0:
        o << entryIndent << "Layered protocol" << endl;
        break;
      case 1:
        o << entryIndent << "Base protocol" << endl;
        break;
      default:
        o << entryIndent << "Protocol chain:" << endl;
        for (int j = 0; j < p.ProtocolChain.ChainLen; ++j) {
          o << chainIndent << p.ProtocolChain.ChainEntries[j] << endl;
        }

        break;
    }

    o << entryIndent << "Version: " << p.iVersion << endl;
    o << entryIndent << "Catalog entry ID: " << p.dwCatalogEntryId << endl;
    wchar_t strGuid[39] = {};
    ::StringFromGUID2(p.ProviderId, strGuid, ArrayLength(strGuid));
    o << entryIndent << "Provider ID: " << ToUTF8(strGuid) << endl;
    o << entryIndent << "Service Flags: 0x" << hex << p.dwServiceFlags1 << endl;
    o << entryIndent << "Provider Flags: 0x" << hex << p.dwProviderFlags
      << endl;

    DWORD categoryInfo = 0;
    size_t categoryInfoSize = sizeof(categoryInfo);
    status =
        WSCGetProviderInfo(&p.ProviderId, ProviderInfoLspCategories,
                           (PBYTE)&categoryInfo, &categoryInfoSize, 0, &errno);
    if (!status) {
      o << entryIndent << "Category Flags: 0x" << hex << categoryInfo << endl;
    }

    o << logIndent << "END ENTRY [" << dec << i << "]" << endl << endl;
  }

  o << "END LOG" << endl;

  WSACleanup();

  if (!useStdout) {
    cout << "Success! Please return lsp.log to Mozilla for analysis." << endl;
  }

  return 0;
}
