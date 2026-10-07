#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <jde/fwk.h>
#include <jde/fwk/settings.h>
#include <jde/app/proto/App.FromServer.pb.h>