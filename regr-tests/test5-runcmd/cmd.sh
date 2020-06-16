#!/bin/bash
echo "executable shell script $0 - $@"
echo '{"msgtype":"alarm","id":1}' > cmd.fifo
