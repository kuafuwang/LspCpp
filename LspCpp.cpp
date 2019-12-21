// JcKitLangClient.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//


#include "LibLsp/lsp/textDocument/signature_help.h"
#include "LibLsp/lsp/general/initialize.h"
#include "LibLsp/lsp/ProtocolJsonHandler.h"
#include "LibLsp/lsp/textDocument/typeHierarchy.h"
#include "LibLsp/lsp/AbsolutePath.h"
#include "LibLsp/lsp/textDocument/resolveCompletionItem.h"
#include <network/uri.hpp>

#ifdef _CONSOLE
#include <boost/filesystem.hpp>
#include <boost/asio.hpp>
#include "sct/ApduModelTest.h"
#include <iostream>

using namespace boost::asio::ip;


string UnicodeToUtf8(const wstring& strUnicode)
{
	int iLen = 0;
	char* pChar = NULL;
	iLen = WideCharToMultiByte(CP_UTF8, 0, strUnicode.c_str(), -1, NULL, 0, NULL, NULL);
	pChar = new char[iLen + 1];
	memset(pChar, 0x00, iLen + 1);
	WideCharToMultiByte(CP_UTF8, 0, strUnicode.c_str(), -1, pChar, iLen, NULL, NULL);
	string strResult(pChar);
	delete[] (pChar);
	return strResult;
}

int test_sct_main() {
	wstring file_path = LR"(C:\workspace\J3R_SC_ECKA\bin\com\nxp\cas\SC\ecka\javacard\ecka.cap)";
	ApduModelTest apdu;
	apdu.startLsp();
	//apdu.Launch();
	//apdu.Connect();
	apdu.DownLoadCapFile(UnicodeToUtf8(file_path));
	std::vector<unsigned char> cmdApdu= {0x00,0xa4,0x00,0x00,0x00};
	std::vector<unsigned char> rspApdu;
	apdu.Transmit(cmdApdu, rspApdu);
	apdu.stop();
	return 0;
}
int main() 
{
	string uri_str = "D:/test  ddd/testdfdf/testdfdf/src/com/nxp/cas/SC/ecka/ECKA.java";
	uri_str = make_file_scheme_uri(uri_str);
	lsDocumentUri doc;
	doc.raw_uri_ = uri_str;
	std::cout << doc.GetRawPath() << std::endl;
	std::cout << doc.GetAbsolutePath().path << std::endl;
	network::uri  uri(uri_str);
	network::uri_builder buider;

	buider.scheme(uri.scheme().to_string());
	buider.authority(uri.authority().to_string());
	buider.path(uri.path().to_string());
	std::cout << buider.uri().string() << std::endl;
	buider.append_query(uri.query().to_string());

	std::cout << buider.uri().string() << std::endl;

	
	std::cout<<"scheme:"  << uri.scheme().to_string() << std::endl;

	std::cout << "authority:" << uri.authority().to_string() << std::endl;
	std::cout << "host:" << uri.host().to_string() << std::endl;
	std::cout << "user_info:" << uri.user_info().to_string() << std::endl;
	std::cout << "port:" << uri.port().to_string() << std::endl;
	std::cout << "path:" << uri.path().to_string() << std::endl;
	std::cout << "query:" << uri.query().to_string() << std::endl;
	std::cout << "fragment:" << uri.fragment().to_string() << std::endl;


}
#endif
