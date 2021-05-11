CXX=g++
OPTFLAGS = -O3 -std=c++17
INCLUDES=-I. -ILibLsp/lsp/extention/jdtls/ -ILibLsp/JsonRpc/ -ILibLsp/JsonRpc/lsp/extention/jdtls
CXXFLAGS = -Wall $(OPTFLAGS) $(INCLUDES)

NETWORKS_DETAIL = $(addprefix detail/, uri_advance_parts.o \
	uri_normalize.o uri_parse.o uri_parse_authority.o uri_resolve.o)

LSP_FILES = general/initialize.o lsp.o lsp_diagnostic.o ProtocolJsonHandler.o textDocument/textDocument.o
NETWORK_FILES = $(addprefix uri/, uri.o uri_builder.o uri_errors.o $(NETWORKS_DETAIL))
JSONRPC_FILES = threaded_queue.o WebSocketServer.o RemoteEndPoint.o \
	Endpoint.o message.o MessageJsonHandler.o serializer.o StreamMessageProducer.o

OFILES = $(addprefix ./network/,$(NETWORK_FILES)) \
	$(addprefix ./LibLsp/lsp/, $(LSP_FILES)) $(addprefix ./LibLsp/JsonRpc/, $(JSONRPC_FILES))

HEADERS = $(shell find ./LibLsp ./network -regex ".*\.\(h\|hpp\)")

default: liblspcpp.a headers.tar.gz

liblspcpp.a: $(OFILES)
	ar -ruv $@ $^

headers.tar.gz: $(HEADERS) macro_map.h optional.h optional.hpp
	tar -czf $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $< -c -o $@

clean:
	find ./ -name *.o | xargs rm -rf
	rm -rf *.a *.tar.gz