#for packet in 1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768
for packet in 1024
do
#for nthread in 1 2 4 8 16 32 64 128 256 512 1024 2048 4096
for nthread in 1024
do

sudo ssh node0 "pkill netperf"
sleep 10

echo "nthread ${nthread} packet ${packet}"
sudo ssh node0 "ulimit -n 8192; cd /proj/osu-nfs-test-PG0/netperf;./netperf -s -t" > log_server_packet${packet}_thread${nthread} 2>&1 &
#sudo ssh node0 "ulimit -n 8192; cd /proj/osu-nfs-test-PG0/netperf;./netperf -s -u" > log_server_packet${packet}_thread${nthread} 2>&1 &
sleep 2

sudo ssh node1 "ulimit -n 8192; cd /proj/osu-nfs-test-PG0/netperf;./netperf -c -t -h 10.10.1.1 -n $nthread -p $packet -d 10" > log_client_packet${packet}_thread${nthread} 2>&1 &
#sudo ssh node1 "ulimit -n 8192; cd /proj/osu-nfs-test-PG0/netperf;./netperf -c -u -h 10.10.1.1 -n $nthread -p $packet -d 10" > log_client_packet${packet}_thread${nthread} 2>&1 &
sleep 17

done
done
