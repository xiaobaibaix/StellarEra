#include "VoxelViewpointInvokers.h"
#include "VoxelComponents/VoxelInvokerComponent.h"

void VoxelViewpointInvokers::SetEnabled(UVoxelInvokerComponentBase* const Invoker, const bool bEnabled)
{
	if (!Invoker || !Invoker->IsRegistered() || Invoker->IsInvokerEnabled() == bEnabled)
	{
		return;
	}

	if (bEnabled)
	{
		Invoker->EnableInvoker();
	}
	else
	{
		Invoker->DisableInvoker();
	}
}
