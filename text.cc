#include <Windows.h>
#include "sigslot.h"

#include <iostream>

using namespace std;
using namespace sigslot;
class Switch {
public:
	Signal0<> Clicked0;
	Signal<SIGSLOT_DEFAULT_MT_POLICY,int> Clickeed;
};

class Light:public has_slots<> {
public:
	void ToggleState(int i) { cout << i << endl; }
	void TurnOn() { cout << "turnon"<< endl; }
	void TurnOff() { cout << "turnon" << endl; }
};

int main(){
	Switch sw1;
	Light l1,l2;
	sw1.Clicked0.connect(&l1, &Light::TurnOn);
	sw1.Clicked0.emit();
	//sw1.Clicked0.disconnect(&l1);
	//sw1.Clicked0.emit();
	sw1.Clickeed.connect(&l2, &Light::ToggleState);
	sw1.Clickeed.emit(2);
	sw1.Clickeed(2);
	sw1.Clickeed.disconnect(&l2);
	sw1.Clickeed.emit(3);
	Light l3(l1);
	sw1.Clicked0();
	Switch sw2(sw1);
	sw2.Clicked0();
	system("pause");
}