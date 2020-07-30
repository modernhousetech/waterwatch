// DON'T USE
// Does not work as intended. We want this "app" specific file to be included
// based on the app we are building. Unfortunely at this time we have only
// fiqured out how to include this file into main.cpp, but not into dapp.cpp.
// We are keeping the file in case we figure out a better mechinism for this.
// For now we use dApp::app_ at run time to add app based funtionality.
#define WWH
#ifdef gpio
extern gpio::GPIOSwitch *valve_open;
extern gpio::GPIOSwitch *valve_close;
#endif
