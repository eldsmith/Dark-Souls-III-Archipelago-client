#include "Core.h"
#include "GameHook.h"
#include <functional>

extern CCore* Core;
extern CItemRandomiser* ItemRandomiser;
extern CGameHook* GameHook;

VOID fItemRandomiser(WorldChrMan* qWorldChrMan, SItemBuffer* pItemBuffer, UINT_PTR pItemData, DWORD64 qReturnAddress) {

	// TODO: this check is unnecessary now and excludes pickle pee items
	if (*(int*)(pItemData) >= 0) ItemRandomiser->RandomiseItem(qWorldChrMan, pItemBuffer, pItemData, qReturnAddress);

	return;
}


VOID CItemRandomiser::RandomiseItem(WorldChrMan* qWorldChrMan, SItemBuffer* pItemBuffer, UINT_PTR pItemData, DWORD64 qReturnAddress) {

	if (pItemBuffer->length > 6) {
		Core->Panic("Too many items!", "...\\Source\\ItemRandomiser\\ItemRandomiser.cpp", FE_AmountTooHigh, 1);
		int3
	};

	int indexToRemove = -1;
	for (int i = 0; i < pItemBuffer->length; i++) {

		SItemBufferEntry* dItem = &pItemBuffer->items[i];

		if (Core->debugLogs) {
			printf("IN itemID : %d\n", dItem->id);
		}

		//Check if the item is received from the server
		if (isReceivedFromServer(dItem->id)) {
			receivedItemsQueue.pop_back();
			//Nothing to do, just let the item go to the player's inventory
			Core->saveConfigFiles = true;
		}
		else if (dItem->id > (0x40000000 + 3780000)) {
			// If we receive a synthetic item generated by the offline randomizer, it may be a
			// placeholder. If so, we need to replace it its real equivalent.
			EquipParamGoodsRow* row = GetGoodsParam(dItem->id & 0xfffffffU);
			if (row->iconId == 7039)
			{
				// If this is a Path of the Dragon replacement, remove it from the item list and grant the
				// gesture manually.
				GameHook->grantPathOfTheDragon();
				indexToRemove = i;
			} else if (row != NULL && row->basicPrice != 0) {
				// Since the function this hooks into only gets called after fItemRandomiser, we
				// have to manually make sure it sees the original synthetic item so it can notify
				// the server that the location was checked.
				OnGetSyntheticItem(row);
				dItem->id = row->basicPrice;
				dItem->quantity = row->sellValue;
				dItem->durability = -1;
			}
		}
		else {
			//Nothing to do, this is a vanilla item so we will let it go to the player's inventory	
		}

		if (Core->debugLogs) {
			printf("OUT itemID : %d\n", dItem->id);
		}
	};

	if (indexToRemove != -1)
	{
		std::memcpy(&pItemBuffer[indexToRemove], &pItemBuffer[indexToRemove + 1],
			(pItemBuffer->length - 1) * sizeof(SItemBufferEntry));
		pItemBuffer->length--;
	}

	return;
}

// This function is called once each time the player receives an item with an ID they don't already
// have in their inventory. It's called _after_ fItemRandomiser, so it'll only see the items that
// that has replaced.
ULONGLONG fOnGetItem(UINT_PTR pEquipInventoryData, DWORD qItemCategory, DWORD qItemID, DWORD qCount, UINT_PTR qUnknown2) {
	// This function is frequently called with very high item IDs while the game is loading
	// for unclear reasons. We want to ignore those calls.
	if (qItemCategory == 0x40000000 && qItemID > 3780000 && qItemID < 0xffffffU) {
		EquipParamGoodsRow* row = GetGoodsParam(qItemID & 0xfffffffU);
		if (row != NULL) {
			ItemRandomiser->OnGetSyntheticItem(row);
		}
	}
	return ItemRandomiser->OnGetItemOriginal(pEquipInventoryData, qItemCategory, qItemID, qCount, qUnknown2);
}

// Tells the Archipelago server that a synthetic item was aquired (meaning that a location was
// visited and possibly that another world's item was received. The itemID should contain only the
// base ID, not the category flag.
//
// Returns the parameter data about the aquired item.
VOID CItemRandomiser::OnGetSyntheticItem(EquipParamGoodsRow* row) {
	int64_t archipelagoLocationId = row->vagrantItemLotId +
		((int64_t)(row->vagrantBonusEneDropItemLotId) << 32);
	checkedLocationsList.push_front(archipelagoLocationId);
}

BOOL CItemRandomiser::isReceivedFromServer(DWORD dItemID) {
	for (SReceivedItem item : receivedItemsQueue) {
		if (dItemID == item.address) {
			return true;
		}
	}
	return false;
}
