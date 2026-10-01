#!/bin/bash
curl -o twsWebSocket-build.sh https://raw.githubusercontent.com/Jde-cpp/TwsWebSocket/master/twsWebSocket-build.sh;
chmod 777 twsWebSocket-build.sh;
cd jde;JDE_DIR=`pwd`;
git clone https://github.com/Jde-cpp/Framework.git;
./Framework/framework-build.sh
git clone https://github.com/Jde-cpp/Odbc.git;
git clone https://github.com/Jde-cpp/IotWebsocket.git;
cd IotWebsocket/scripts