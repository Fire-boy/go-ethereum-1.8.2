sleep 5
echo "start restart....."
sudo kill -SIGUSR2 `cat nginx.pid` 
sleep  30
sudo kill -SIGQUIT `cat nginx.pid.oldbin`
