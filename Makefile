CXX=g++
OPTFLAGS = -O3 -std=c++17
INCLUDES=-I. -ILibLsp/lsp/extention/jdtls/ -ILibLsp/JsonRpc/ -ILibLsp/JsonRpc/lsp/extention/jdtls \
	-Ithird_party/threadpool
CXXFLAGS = -Wall $(OPTFLAGS) $(INCLUDES)

NETWORKS_DETAIL = $(addprefix detail/, uri_advance_parts.o \
	uri_normalize.o uri_parse.o uri_parse_authority.o uri_resolve.o)
NETWORK_FILES = $(addprefix uri/, uri.o uri_builder.o uri_errors.o $(NETWORKS_DETAIL))
LSP_FILES = extention/sct/sct.o general/initialize.o lsp.o lsp_diagnostic.o \
	ProtocolJsonHandler.o textDocument/textDocument.o markup/Markup.o ParentProcessWatcher.cpp \
	utils.o working_files.o
JSONRPC_FILES = TcpServer.o threaded_queue.o WebSocketServer.o RemoteEndPoint.o \
	Endpoint.o message.o MessageJsonHandler.o serializer.o StreamMessageProducer.o \
	Context.o

OFILES = $(addprefix ./network/,$(NETWORK_FILES)) \
	$(addprefix ./LibLsp/lsp/, $(LSP_FILES)) \
	$(addprefix ./LibLsp/JsonRpc/, $(JSONRPC_FILES))

HEADERS = $(shell find ./LibLsp ./network -regex ".*\.\(h\|hpp\)")

default: liblspcpp.a headers.tar.gz

liblspcpp.a: $(OFILES)
	ar -r $@ $^

headers.tar.gz: $(HEADERS) macro_map.h
	tar -czf $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $< -c -o $@

clean:
	find ./ -name *.o | xargs rm -rf
	rm -rf *.a *.tar.gz