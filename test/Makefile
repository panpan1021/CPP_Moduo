all: server client
client:tcp_cli.cc
	g++ -std=c++11 tcp_cli.cc -o client
server:tcp_server.cc
	g++ -std=c++11 tcp_server.cc -o server
clean:
	rm -rf client server