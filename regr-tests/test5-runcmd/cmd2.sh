set -e
echo "shell script $0 - $@"
echo '{"msgtype":"alarm","id":2}' > cmd.fifo

