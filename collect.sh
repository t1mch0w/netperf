for packet in 1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768
do
for nthread in 1 2 4 8 16 32 64 128 256 512 1024 2048 4096
do
echo $packet $nthread $(grep throughput log*_packet${packet}_thread${nthread} | awk '{print $NF}')
done
done
