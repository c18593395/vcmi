/*
 * ServerRunner.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#ifndef NOKERNEL
#define NOKERNEL
#endif
#ifndef NOSOUND
#define NOSOUND
#endif
#include <windows.h> // [THREAD] 插桩 GetCurrentThreadId (09-09)
#else
static inline unsigned long GetCurrentThreadId() { return 0; }
#endif

#include "ServerRunner.h"

#include "../lib/VCMIDirs.h"
#include "../lib/CThreadHelper.h"
#include "../lib/network/NetworkInterface.h"
#include "../lib/CConfigHandler.h"
#include "../server/CVCMIServer.h"

#ifdef ENABLE_SERVER_PROCESS

#if BOOST_VERSION >= 108600
// TODO: upgrade code to use v2 API instead of deprecated v1
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/io.hpp>
#else
#include <boost/process/child.hpp>
#include <boost/process/io.hpp>
#endif

#endif

#include <future>

ServerThreadRunner::ServerThreadRunner() = default;
ServerThreadRunner::~ServerThreadRunner() = default;

void ServerThreadRunner::start(bool listenForConnections, bool connectToLobby, std::shared_ptr<StartInfo> startingInfo)
{
	// cfgport may be 0 -- the real port is returned after calling prepare()
	uint16_t port = settings["server"]["localPort"].Integer();
	server = std::make_unique<CVCMIServer>(port, true);
	lobbyMode = connectToLobby;

	if (startingInfo)
	{
		server->si = startingInfo; //Else use default
	}

	std::promise<uint16_t> promise;

	threadRunLocalServer = std::thread([this, connectToLobby, listenForConnections, &promise]{
		setThreadName("runServer");
		// 0909 插桩: server 线程生命周期
		fprintf(stderr, "[THREAD] runServer ENTER tid=%lu\n", GetCurrentThreadId()); fflush(stderr);
		uint16_t port = server->prepare(connectToLobby, listenForConnections);
		promise.set_value(port);
		// 0910: exception escaping this thread hits SEH filter ("Disaster happened") and leaves a zombie process - log reason and exit cleanly instead
		try
		{
			server->run();
		}
		catch (const std::exception & e)
		{
			logGlobal->error("runServer thread terminated by exception: %s", e.what());
			fprintf(stderr, "[THREAD] runServer THREW: %s tid=%lu\n", e.what(), GetCurrentThreadId()); fflush(stderr);
		}
		catch (...)
		{
			logGlobal->error("runServer thread terminated by unknown exception");
			fprintf(stderr, "[THREAD] runServer THREW: unknown tid=%lu\n", GetCurrentThreadId()); fflush(stderr);
		}
		fprintf(stderr, "[THREAD] runServer EXIT tid=%lu\n", GetCurrentThreadId()); fflush(stderr);
	});

	logNetwork->trace("Waiting for server port...");
	serverPort = promise.get_future().get();
	logNetwork->debug("Server port: %d", serverPort);
}

void ServerThreadRunner::shutdown()
{
	server->setState(EServerState::SHUTDOWN);
}

void ServerThreadRunner::wait()
{
	threadRunLocalServer.join();
}

int ServerThreadRunner::exitCode()
{
	return 0;
}

void ServerThreadRunner::connect(INetworkHandler & network, INetworkClientListener & listener)
{
	if (lobbyMode)
	{
		std::string host = settings["server"]["localHostname"].String();
		uint16_t port = settings["server"]["localPort"].Integer();
		logNetwork->info("Establishing connection to %s:%d...", host, port);

		network.connectToRemote(listener, host, port);
	}
	else
	{
		network.createInternalConnection(listener, server->getNetworkServer());
	}
}

#ifdef ENABLE_SERVER_PROCESS

ServerProcessRunner::ServerProcessRunner() = default;
ServerProcessRunner::~ServerProcessRunner() = default;

void ServerProcessRunner::shutdown()
{
	child->terminate();
}

void ServerProcessRunner::wait()
{
	child->wait();
}

int ServerProcessRunner::exitCode()
{
	return child->exit_code();
}

void ServerProcessRunner::start(bool listenForConnections, bool connectToLobby, std::shared_ptr<StartInfo> startingInfo)
{
	uint16_t port = settings["server"]["localPort"].Integer();
	boost::filesystem::path serverPath = VCMIDirs::get().serverPath();
	boost::filesystem::path logPath = VCMIDirs::get().userLogsPath() / "server_log.txt";
	std::vector<std::string> args;
	args.push_back("--port=" + std::to_string(port));
	args.push_back("--run-by-client");
	if(connectToLobby)
		args.push_back("--lobby");

	std::error_code ec;
	child = std::make_unique<boost::process::child>(serverPath, args, ec, boost::process::std_out > logPath);

	if (ec)
		throw std::runtime_error("Failed to start server! Reason: " + ec.message());
}

void ServerProcessRunner::connect(INetworkHandler & network, INetworkClientListener & listener)
{
	std::string host = settings["server"]["localHostname"].String();
	uint16_t port = settings["server"]["localPort"].Integer();
	logNetwork->info("Establishing connection to %s:%d...", host, port);

	network.connectToRemote(listener, host, port);
}

#endif
