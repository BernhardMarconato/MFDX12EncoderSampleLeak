#include "pch.h"
#include "MFEncodingSample.h"

using namespace winrt;
using namespace Windows::Foundation;

int main()
{
    init_apartment();

	MFEncodingSample encodingSample{};
	encodingSample.RunRepro();
}
