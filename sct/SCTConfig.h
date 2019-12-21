#pragma once
#include <string>
#include <vector>
#include <LibLsp/JsonRpc/serializer.h>
using namespace std;


struct TCP_option
{
	std::string host = "127.0.0.1";
	int port = 8889;
	
};
MAKE_REFLECT_STRUCT(TCP_option,host,port)


struct SCTConfig
{
	static SCTConfig* newInstance(const wstring& file_path,string& error);
	std::string version;
	std::string file_name;
	
	optional<bool> start_by_jcide;
	
	optional<TCP_option> tcp;
	optional<vector<string>> args;
	
	// internal using
	bool broken = false;
	std::string error;
};


MAKE_REFLECT_STRUCT(SCTConfig, version,file_name, tcp, args, start_by_jcide);



