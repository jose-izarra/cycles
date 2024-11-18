export CYCLES_PORT=3101


cat<<EOF> config.yaml
gameHeight: 800
gameWidth: 1000
gameBannerHeight: 100
gridHeight: 100
gridWidth: 100
maxClients: 60
enablePostProcessing: false
EOF

./build/bin/server &
SERVER_PID=$!

sleep 1

for i in {1..4}
do
./build/bin/client randomio$i &
done

./build/bin/client_jose jose
