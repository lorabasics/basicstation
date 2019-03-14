TD=../..
platform?=linux
variant?=testsim

all: station sim

prep prep.done:
	./prep.sh

station:
	platform=${platform} variant=${variant} make -C ${TD}

sim: station prep.done
	PATH=${TD}/build-${platform}-${variant}/bin:${PATH} python sim.py runstation

emulate: prep.done
	./emulate.sh

tmux: station prep.done
	@if [ -z "${TMUX}" ]; then \
		echo "Starting new tmux session" ; \
		tmux new-session -d 'python sim.py'; \
		tmux split-window -v 'sleep 1; until ${TD}/build-${platform}-${variant}/bin/station  -h ./shome -t ./shome -l DEBUG; do echo -e "\e[31m== Station stopped. Restarting in 5 Second. Hit CTRL-C to terminate.\e[0m" ; sleep 5; done'; \
		tmux -2 attach-session -d; \
		tmux select-layout even-vertical; \
	else \
		echo "Creating new window inside tmux session"; \
		tmux new-window 'python sim.py'; \
		tmux split-window -v 'sleep 1; until ${TD}/build-${platform}-${variant}/bin/station  -h ./shome -t ./shome -l DEBUG; do echo -e "\e[31m== Station stopped. Restarting in 5 Second. Hit CTRL-C to terminate.\e[0m" ; sleep 5; done'; \
		tmux select-layout even-vertical; \
	fi

clean:
	cd shome && (rm -rf tc* station.log station.pid spidev cups* version.txt sig*)
	rm -rf ca cups-* tc* upd-sig prep.done

.PHONY: all clean prep tmux emulate sim station
