## How to build station for "corecell" platform 
* Copy or clone this repo to the corecell platform, which is a Raspberry Pi
* Execute the below command from the top folder for debug variant
  ```
   make platform=corecell variant=debug
  ```
* After build finishes the corecell build artifacts can be found at "build-corecell-debug" folder
* For standard build (less debug prints) use 'variant=std' e.g.
  ```
   make platform=corecell variant=std
  ```

## Example to Test with "TTN" LNS with variant "std"
``` sourceCode
cd example/corecell
./start-station.sh -l ./lns-ttn
```
## Example to Test with "TTN" LNS with variant "debug"
``` sourceCode
cd example/corecell
./start-station.sh -dl ./lns-ttn
```
