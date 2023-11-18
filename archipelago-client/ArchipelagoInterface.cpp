#include <sstream>

#include "ArchipelagoInterface.h"

#ifdef __EMSCRIPTEN__
#define DATAPACKAGE_CACHE "/settings/datapackage.json"
#define UUID_FILE "/settings/uuid"
#else
#define DATAPACKAGE_CACHE "datapackage.json" // TODO: place in %appdata%
#define UUID_FILE "uuid" // TODO: place in %appdata%
#endif

extern CCore* Core;
extern CItemRandomiser* ItemRandomiser;
extern CGameHook* GameHook;

bool ap_sync_queued = false;
APClient* ap;

BOOL CArchipelago::Initialise(std::string URI) {
	
	Core->Logger("CArchipelago::Initialise", true, false);

	// read or generate uuid, required by AP
	std::string uuid = ap_get_uuid(UUID_FILE);
	if (ap != nullptr) {
		ap->reset();
	}

	ap = new APClient(uuid, "Dark Souls III", URI);

	ap_sync_queued = false;
	ap->set_socket_connected_handler([]() {
		});
	ap->set_socket_disconnected_handler([]() {
		});
	ap->set_slot_connected_handler([](const json& data) {
		Core->Logger("Slot connected successfully, reading slot data ... ");
		// read the archipelago slot data

		//Mandatory values
		if (!data.contains("apIdsToItemIds") || !data.contains("itemCounts") || !data.contains("seed") || !data.contains("slot")) {
			Core->Panic("Please check the following values : [apItemsToItemIds], [seed] and [slot]", "One of the mandatory values is missing in the slot data", AP_MissingValue, 1);
		}

		std::map<std::string, DWORD> map;
		data.at("apIdsToItemIds").get_to(map);
		for (std::map<std::string, DWORD>::iterator it = map.begin(); it != map.end(); ++it) {
			ItemRandomiser->pApItemsToItemIds[std::stol(it->first)] = it->second;
		}

		map.clear();
		data.at("itemCounts").get_to(map);
		for (std::map<std::string, DWORD>::iterator it = map.begin(); it != map.end(); ++it) {
			ItemRandomiser->pItemCounts[std::stol(it->first)] = it->second;
		}
		
		data.at("seed").get_to(Core->pSeed);
		data.at("slot").get_to(Core->pSlotName);

		if (data.contains("options")) {
			(data.at("options").contains("auto_equip")) ? (data.at("options").at("auto_equip").get_to(ItemRandomiser->dIsAutoEquip)) : ItemRandomiser->dIsAutoEquip = false;
			(data.at("options").contains("lock_equip")) ? (data.at("options").at("lock_equip").get_to(GameHook->dLockEquipSlots)) : GameHook->dLockEquipSlots = false;
			(data.at("options").contains("death_link")) ? (data.at("options").at("death_link").get_to(GameHook->dIsDeathLink)) : GameHook->dIsDeathLink = false;
			(data.at("options").contains("enable_dlc")) ? (data.at("options").at("enable_dlc").get_to(GameHook->dEnableDLC)) : GameHook->dEnableDLC = false;
		}

		std::list<std::string> tags;
		if (GameHook->dIsDeathLink) { 
			tags.push_back("DeathLink"); 
			ap->ConnectUpdate(false, 1, true, tags);
		}

		});
	ap->set_slot_disconnected_handler([]() {
		Core->Logger("Slot disconnected");

		GameHook->showMessage(L"Archipelago disconnected! Don't pick up any items until it reconnects");

		});
	ap->set_slot_refused_handler([](const std::list<std::string>& errors){
		for (const auto& error : errors) {
			Core->Logger("Connection refused : " + error);
		}
		GameHook->showMessage(L"Archipelago connection refused!");
		});

	ap->set_room_info_handler([]() {
		std::list<std::string> tags;
		if (GameHook->dIsDeathLink) { tags.push_back("DeathLink"); }
		ap->ConnectSlot(Core->pSlotName, Core->pPassword, 5, tags, { 0,4,2 });
		});

	ap->set_items_received_handler([](const std::list<APClient::NetworkItem>& items) {

		if (!ap->is_data_package_valid()) {
			// NOTE: this should not happen since we ask for data package before connecting
			if (!ap_sync_queued) ap->Sync();
			ap_sync_queued = true;
			return;
		}

		for (const auto& item : items) {
			std::string itemname = ap->get_item_name(item.item);
			std::string sender = ap->get_player_alias(item.player);
			std::string location = ap->get_location_name(item.location);

			//Check if we should ignore this item
			if (item.index < Core->pLastReceivedIndex) {
				continue;
			}

			std::ostringstream stringStream;
			stringStream << "#" << item.index << ": " << itemname.c_str() << " from " << sender.c_str() << " - " << location.c_str();
			std::string itemDesc = stringStream.str();

			//Add the item to the list of already received items, only for logging purpose
			Core->pReceivedItems.push_back(itemDesc);
			Core->Logger(itemDesc);

			//Determine the item address
			auto ds3IdSearch = ItemRandomiser->pApItemsToItemIds.find(item.item);
			if (ds3IdSearch == ItemRandomiser->pApItemsToItemIds.end()) {
				Core->Logger("The following item has not been found in the item pool. Please check your seed options : " + itemname);
				continue;
			}

			auto countSearch = ItemRandomiser->pItemCounts.find(item.item);
			ItemRandomiser->receivedItemsQueue.push_front({
				ds3IdSearch->second,
				countSearch == ItemRandomiser->pItemCounts.end() ? 1 : countSearch->second
			});
		}
		});

	//TODO :   * you can still use `set_data_package` or `set_data_package_from_file` during migration to make use of the old cache

	ap->set_print_handler([](const std::string& msg) {
		Core->Logger(msg);
		GameHook->showMessage(msg);
		});

	ap->set_print_json_handler([](const std::list<APClient::TextNode>& msg) {
		Core->Logger(ap->render_json(msg, APClient::RenderFormat::TEXT));
		});

	ap->set_bounced_handler([](const json& cmd) {
		if (GameHook->dIsDeathLink) {
			Core->Logger("Received DeathLink", true, false);
			auto tagsIt = cmd.find("tags");
			auto dataIt = cmd.find("data");
			if (tagsIt != cmd.end() && tagsIt->is_array()
				&& std::find(tagsIt->begin(), tagsIt->end(), "DeathLink") != tagsIt->end())
			{
				if (dataIt != cmd.end() && dataIt->is_object()) {
					json data = *dataIt;
					if (data["source"].get<std::string>() != Core->pSlotName) {
						
						std::string source = data["source"].is_string() ? data["source"].get<std::string>().c_str() : "???";
						std::string cause = data["cause"].is_string() ? data["cause"].get<std::string>().c_str() : "???";
						std::string message = "Died by the hands of " + source + " : " + cause;
						Core->Logger(message);
						GameHook->showMessage(message);

						GameHook->deathLinkData = true;
					}
				}
				else {
					Core->Logger("Bad deathlink packet!", true, false);
				}
			}
		}
		});
	
	return true;
}


VOID CArchipelago::say(std::string message) {
	if (ap && ap->get_state() == APClient::State::SLOT_CONNECTED) {
		ap->Say(message);
	}
}


BOOLEAN CArchipelago::isConnected() {
	return ap && ap->get_state() == APClient::State::SLOT_CONNECTED;
}

VOID CArchipelago::update() {

	if (ap) ap->poll();

	int size = ItemRandomiser->checkedLocationsList.size();
	if (ap && size > 0) {
		if (ap->LocationChecks(ItemRandomiser->checkedLocationsList)) {
			Core->Logger(size + " checks sent successfully", true, false);
			ItemRandomiser->checkedLocationsList.clear();
		}
		else {
			Core->Logger(size + " checks has not been sent and will be kept in queue");
		}
	}
}

VOID CArchipelago::gameFinished() {
	if (ap) ap->StatusUpdate(APClient::ClientStatus::GOAL);
}

VOID CArchipelago::sendDeathLink() {
	if (!ap || !GameHook->dIsDeathLink) return;

	Core->Logger("Sending deathlink");

	json data{
		{"time", ap->get_server_time()},
		{"cause", "Dark Souls III."},
		{"source", ap->get_slot()},
	};
	ap->Bounce(data, {}, {}, { "DeathLink" });
}
