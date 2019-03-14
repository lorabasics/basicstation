TD=../..
platform?=linux
variant?=testsim

all: station sim

station:
	platform=${platform} variant=${variant} make -C ${TD}

sim: station
	PATH=${TD}/build-${platform}-${variant}/bin:${PATH} python sim.py

tmux: station
	@if [ -z "${TMUX}" ]; then \
		echo "Starting new tmux session" ; \
		tmux new-session -d 'python sim.py tc'; \
		tmux split-window -v 'python sim.py lgwsim'; \
		tmux split-window -v 'sleep 1; ${TD}/build-${platform}-${variant}/bin/station  -l DEBUG'; \
		tmux -2 attach-session -d; \
		tmux select-layout even-vertical; \
	else \
		echo "Creating new window inside tmux session"; \
		tmux new-window 'python sim.py tc'; \
		tmux split-window -v 'python sim.py lgwsim'; \
		tmux split-window -v 'sleep 1; ${TD}/build-${platform}-${variant}/bin/station  -l DEBUG'; \
		tmux select-layout even-vertical; \
	fi

clean:
	rm -rf tc-* station.log station.pid spidev

.PHONY: all clean
