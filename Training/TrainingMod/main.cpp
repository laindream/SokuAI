#include <windows.h>
#include <shlwapi.h>
#include <SokuLib.hpp>
#include <thread>
#include <optional>
#include <map>
#include <process.h>
#include "Packet.hpp"
#include "Exceptions.hpp"

#define CHECK_PACKET_SIZE(requested_type, size) if (size != sizeof(requested_type)) return sendError(Trainer::ERROR_INVALID_PACKET)
#define printf(...)
#define puts(s)

static int (SokuLib::Battle::*s_origCBattle_OnRenderAddr)();
static int (SokuLib::Select::*s_origCSelect_OnRenderAddr)();
static int (SokuLib::Loading::*s_origCLoading_OnRenderAddr)();
static int (SokuLib::Title::*s_origCTitle_OnRenderAddr)();
static int (SokuLib::Logo::*s_origCLogo_OnProcess)();
static int (SokuLib::Battle::*s_origCBattle_OnProcess)();
static int (SokuLib::Select::*s_origCSelect_OnProcess)();
int (__fastcall *og_loadTexture)(unsigned *, char *, int **, unsigned *);
static void (SokuLib::KeymapManager::*s_origKeymapManager_SetInputs)();

static SOCKET sock;
static bool setWeather = false;
static bool freezeWeather = false;
static SokuLib::Weather weather;
static unsigned short weatherTimer;
static bool hello = false;
static bool stop = false;
static bool gameFinished = false;
static bool displayed = true;
static bool begin = true;
static bool cancel = false;

static bool startRequested = false;
volatile bool inputsReceived = false;

static float counter = 0;
static unsigned tps = 60;
static unsigned char decks[40];
static std::pair<Trainer::Input, Trainer::Input> inputs;
static std::pair<SokuLib::KeyInput, SokuLib::KeyInput> lastInputs;
static const std::map<SokuLib::Character, std::vector<unsigned short>> characterSpellCards{
	{SokuLib::CHARACTER_ALICE, {200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211}},
	{SokuLib::CHARACTER_AYA, {200, 201, 202, 203, 205, 206, 207, 208, 211, 212}},
	{SokuLib::CHARACTER_CIRNO, {200, 201, 202, 203, 204, 205, 206, 207, 208, 210, 213}},
	{SokuLib::CHARACTER_IKU, {200, 201, 202, 203, 206, 207, 208, 209, 210, 211}},
	{SokuLib::CHARACTER_KOMACHI, {200, 201, 202, 203, 204, 205, 206, 207, 211}},
	{SokuLib::CHARACTER_MARISA, {200, 202, 203, 204, 205, 206, 207, 208, 209, 211, 212, 214, 215, 219}},
	{SokuLib::CHARACTER_MEILING, {200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 211}},
	{SokuLib::CHARACTER_PATCHOULI, {200, 201, 202, 203, 204, 205, 206, 207, 210, 211, 212, 213}},
	{SokuLib::CHARACTER_REIMU, {200, 201, 204, 206, 207, 208, 209, 210, 214, 219}},
	{SokuLib::CHARACTER_REMILIA, {200, 201, 202, 203, 204, 205, 206, 207, 208, 209}},
	{SokuLib::CHARACTER_SAKUYA, {200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212}},
	{SokuLib::CHARACTER_SANAE, {200, 201, 202, 203, 204, 205, 206, 207, 210}},
	{SokuLib::CHARACTER_SUIKA, {200, 201, 202, 203, 204, 205, 206, 207, 208, 212}},
	{SokuLib::CHARACTER_SUWAKO, {200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 212}},
	{SokuLib::CHARACTER_TENSHI, {200, 201, 202, 203, 204, 205, 206, 207, 208, 209}},
	{SokuLib::CHARACTER_REISEN, {200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211}},
	{SokuLib::CHARACTER_UTSUHO, {200, 201, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214}},
	{SokuLib::CHARACTER_YOUMU, {200, 201, 202, 203, 204, 205, 206, 207, 208, 212}},
	{SokuLib::CHARACTER_YUKARI, {200, 201, 202, 203, 204, 205, 206, 207, 208, 215}},
	{SokuLib::CHARACTER_YUYUKO, {200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 219}}
};

void updateInput(SokuLib::KeyInput &old, const Trainer::Input &n) {
	old.a = n.A ? (old.a + 1) : 0;
	old.b = n.B ? (old.b + 1) : 0;
	old.c = n.C ? (old.c + 1) : 0;
	old.d = n.D ? (old.d + 1) : 0;
	old.changeCard = n.SW ? (old.changeCard + 1) : 0;
	old.spellcard = n.SC ? (old.spellcard + 1) : 0;

	if (n.H == 0)
		old.horizontalAxis = 0;
	else if (n.H > 0)
		old.horizontalAxis = max(0, old.horizontalAxis) + 1;
	else
		old.horizontalAxis = min(0, old.horizontalAxis) - 1;

	if (n.V == 0)
		old.verticalAxis = 0;
	else if (n.V > 0)
		old.verticalAxis = max(0, old.verticalAxis) + 1;
	else
		old.verticalAxis = min(0, old.verticalAxis) - 1;
}

static void fillState(const SokuLib::CharacterManager &source, const SokuLib::CharacterManager &opponent, Trainer::CharacterState &destination)
{
	destination.direction = source.objectBase.direction;
	destination.opponentRelativePos.y = source.objectBase.position.y - opponent.objectBase.position.y;
	if (source.objectBase.direction == SokuLib::LEFT) {
		destination.opponentRelativePos.x = source.objectBase.position.x - opponent.objectBase.position.x;
		destination.distToBackCorner = 1240 - source.objectBase.position.x;
		destination.distToFrontCorner = source.objectBase.position.x - 40;
	} else {
		destination.opponentRelativePos.x = opponent.objectBase.position.x - source.objectBase.position.x;
		destination.distToBackCorner = source.objectBase.position.x - 40;
		destination.distToFrontCorner = 1240 - source.objectBase.position.x;
	}
	destination.distToGround = source.objectBase.position.y;
	destination.action = source.objectBase.action;
	destination.actionBlockId = source.objectBase.actionBlockId;
	destination.animationCounter = source.objectBase.animationCounter;
	destination.animationSubFrame = source.objectBase.animationSubFrame;
	destination.frameCount = source.objectBase.frameCount;
	destination.comboDamage = source.combo.damages;
	destination.comboLimit = source.combo.limit;
	destination.airBorne = source.objectBase.frameData.frameFlags.airborne;
	destination.hp = source.objectBase.hp;
	destination.airDashCount = source.airdashCount;
	destination.spirit = source.currentSpirit;
	destination.maxSpirit = source.maxSpirit;
	destination.untech = source.untech;
	destination.healingCharm = source.healingCharmTimeLeft;
	destination.swordOfRapture = source.swordOfRaptureDebuffTimeLeft;
	destination.score = source.score;
	memset(destination.hand, 0xFF, sizeof(destination.hand));
	if (SokuLib::activeWeather != SokuLib::WEATHER_MOUNTAIN_VAPOR)
		for (int i = 0; i < source.hand.size; i++)
			destination.hand[i] = source.hand[i].id;
	destination.cardGauge = (SokuLib::activeWeather != SokuLib::WEATHER_MOUNTAIN_VAPOR ? source.cardGauge : -1);
	memcpy(destination.skills, source.skillMap, sizeof(destination.skills));
	destination.fanLevel = source.tenguFans;
	destination.dropInvulTimeLeft = source.dropInvulTimeLeft;
	destination.superArmorTimeLeft = 0;//TODO: source.superArmorTimeLeft;
	destination.superArmorHp = 0;//TODO: source.superArmorHp;
	destination.milleniumVampireTimeLeft = source.milleniumVampireTime;
	destination.philosoferStoneTimeLeft = source.philosophersStoneTime;
	destination.sakuyasWorldTimeLeft = source.sakuyasWorldTime;
	destination.privateSquareTimeLeft = source.privateSquare;
	destination.orreriesTimeLeft = source.orreriesTimeLeft;
	destination.mppTimeLeft = source.missingPurplePowerTimeLeft;
	destination.kanakoCooldown = source.kanakoTimeLeft;
	destination.suwakoCooldown = source.suwakoTimeLeft;
	destination.objectCount = source.objects.list.size;
}

static void fillState(const SokuLib::ObjectManager &object, const SokuLib::CharacterManager &source, const SokuLib::CharacterManager &opponent, Trainer::Object &destination)
{
	destination.direction = object.direction;
	destination.relativePosMe.x = object.position.x - source.objectBase.position.x;
	destination.relativePosMe.y = object.position.y - source.objectBase.position.y;
	destination.relativePosOpponent.x = object.position.x - opponent.objectBase.position.x;
	destination.relativePosOpponent.y = object.position.y - opponent.objectBase.position.y;
	destination.action = object.action;
	destination.imageID = object.image.number;
}

bool isFrame = false;

void __fastcall KeymapManagerSetInputs(SokuLib::KeymapManager *This)
{
	puts("Input");
	(This->*s_origKeymapManager_SetInputs)();
	if (SokuLib::sceneId != SokuLib::newSceneId) {
		puts("Diff");
		return;
	}
	if (begin && SokuLib::sceneId == SokuLib::SCENE_TITLE) {
		begin = false;
		memset(&This->input, 0, sizeof(This->input));
		This->input.a = 1;
	}
	if (startRequested || gameFinished) {
		memset(&This->input, 0, sizeof(This->input));
		This->input.a = 1;
	}
	if (isFrame) {
		auto &battle = SokuLib::getBattleMgr();
		Trainer::Input *input;
		SokuLib::KeyInput *lastInput;
		static bool left = true;

		if (left) {
			input = &inputs.first;
			lastInput = &lastInputs.first;
		} else  {
			input = &inputs.second;
			lastInput = &lastInputs.second;
		}
		left = !left;
		updateInput(*lastInput, *input);
		memcpy(&This->input, lastInput, sizeof(*lastInput));
	}
	puts("End");
	//static bool lasts = false;
	//if (SokuLib::sceneId == SokuLib::SCENE_SELECTCL || SokuLib::sceneId == SokuLib::SCENE_SELECTSV) {
	//	memset(&This->input, 0, sizeof(This->input));
	//	This->input.a = lasts;

	//	lasts = !lasts;

	//	SokuLib::leftPlayerInfo.character = SokuLib::CHARACTER_REMILIA;
	//	SokuLib::leftPlayerInfo.palette = 0;
	//	SokuLib::leftPlayerInfo.deck = 0;
	//	SokuLib::rightPlayerInfo.character = SokuLib::CHARACTER_REMILIA;
	//	SokuLib::rightPlayerInfo.palette = 5;
	//	SokuLib::rightPlayerInfo.deck = 0;
	//	*(unsigned *)(*(unsigned *)0x8A000C + 0x2438) = 13;// Stage
	//	*(unsigned *)(*(unsigned *)0x8A000C + 0x244C) = 13;// Music
	//}
}

int dummyFunction()
{
	return 1;
}

static void sendError(Trainer::Errors code)
{
	Trainer::ErrorPacket packet;
	packet.op = Trainer::OPCODE_ERROR;
	packet.error = code;
	::send(sock, reinterpret_cast<const char *>(&packet), sizeof(packet), 0);
}

static void sendOpcode(Trainer::Opcode code)
{
	::send(sock, reinterpret_cast<const char *>(&code), sizeof(code), 0);
}

int __fastcall fakeLoad(unsigned *puParm1, char *pcParm2, int **param_3, unsigned *param_4)
{
	return og_loadTexture(puParm1, "empty.png", param_3, param_4);
}

static void swapDisplay(bool enabled)
{
	*(char *)0x89ffbd = !enabled;
	if (!enabled == !displayed)
		return;

	DWORD old;

	displayed = enabled;
	//::VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old);
	VirtualProtect((PVOID)TEXT_SECTION_OFFSET, TEXT_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old);
	if (!displayed) {
		og_loadTexture = reinterpret_cast<int (__fastcall *)(unsigned *, char *, int **, unsigned *)>(SokuLib::TamperNearJmpOpr(0x40505c, fakeLoad));
	//	s_origCTitle_OnRenderAddr = SokuLib::TamperDword(&SokuLib::VTable_Title.onRender, dummyFunction);
	//	s_origCBattle_OnRenderAddr = SokuLib::TamperDword(&SokuLib::VTable_Battle.onRender, dummyFunction);
	//	s_origCSelect_OnRenderAddr = SokuLib::TamperDword(&SokuLib::VTable_Select.onRender, dummyFunction);
	//	s_origCLoading_OnRenderAddr = SokuLib::TamperDword(&SokuLib::VTable_Loading.onRender, dummyFunction);
	} else {
		SokuLib::TamperNearJmpOpr(0x40505c, og_loadTexture);
	//	SokuLib::TamperDword(&SokuLib::VTable_Title.onRender, s_origCTitle_OnRenderAddr);
	//	SokuLib::TamperDword(&SokuLib::VTable_Battle.onRender, s_origCBattle_OnRenderAddr);
	//	SokuLib::TamperDword(&SokuLib::VTable_Select.onRender, s_origCSelect_OnRenderAddr);
	//	SokuLib::TamperDword(&SokuLib::VTable_Loading.onRender, s_origCLoading_OnRenderAddr);
	}
	VirtualProtect((PVOID)TEXT_SECTION_OFFSET, TEXT_SECTION_SIZE, old, &old);
	//::VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, old, &old);
}

static void startGame(const Trainer::StartGamePacket &startData)
{
	SokuLib::leftPlayerInfo.character = startData.leftCharacter;
	SokuLib::rightPlayerInfo.character = startData.rightCharacter;
	SokuLib::leftPlayerInfo.palette = startData.leftPalette;
	SokuLib::rightPlayerInfo.palette = startData.rightPalette;

	sendOpcode(Trainer::OPCODE_OK);
	SokuLib::changeScene(SokuLib::SCENE_SELECT);
	SokuLib::waitForSceneChange();
	SokuLib::setBattleMode(SokuLib::BATTLE_MODE_VSPLAYER, SokuLib::BATTLE_SUBMODE_PLAYING2);

	gameFinished = false;
	cancel = false;
	inputsReceived = false;
	startRequested = true;

	memset(&lastInputs.first, 0, sizeof(lastInputs.first));
	memset(&lastInputs.second, 0, sizeof(lastInputs.second));

	SokuLib::profile1.name = "SokuAI";
	SokuLib::profile1.file = "SokuAI.pf";
	SokuLib::profile2.name = "SokuAI";
	SokuLib::profile2.file = "SokuAI.pf";

	SokuLib::leftPlayerInfo.character = startData.leftCharacter;
	SokuLib::rightPlayerInfo.character = startData.rightCharacter;
	SokuLib::leftPlayerInfo.palette = startData.leftPalette;
	SokuLib::rightPlayerInfo.palette = startData.rightPalette;

	memcpy(decks, startData.leftDeck, 20);
	memcpy(decks + 20, startData.rightDeck, 20);

	SokuLib::currentScene->to<SokuLib::Select>().selectedStage = startData.stageId;// Stage
	SokuLib::currentScene->to<SokuLib::Select>().selectedMusic = startData.musicId;// Music
	((unsigned *)0x00898680)[0] = 0x008986A8; //Init both inputs
	((unsigned *)0x00898680)[1] = 0x008986A8; //Init both inputs
}

static bool isDeckValid(SokuLib::Character chr, const unsigned char (&deck)[20])
{
	std::map<unsigned short, unsigned char> found;
	const auto &validCards = characterSpellCards.at(chr);
	const unsigned char maxSkillId = 112 + (chr == SokuLib::CHARACTER_PATCHOULI) * 3;

	for (auto card : deck) {
		auto &elem = found[card];

		elem++;
		if (elem > 4)
			return false;
		if (card <= 20)
			continue;
		if (card < 100)
			return false;
		if (card <= maxSkillId)
			continue;
		if (std::find(validCards.begin(), validCards.end(), card) == validCards.end())
			return false;
	}
	return true;
}

static bool verifyStartData(const Trainer::StartGamePacket &packet)
{
	if (SokuLib::sceneId != SokuLib::SCENE_TITLE)
		return sendError(Trainer::ERROR_STILL_PLAYING), false;
	if (packet.stageId > 19)
		return sendError(Trainer::ERROR_INVALID_STAGE), false;
	if (packet.musicId > 19)
		return sendError(Trainer::ERROR_INVALID_MUSIC), false;
	if (packet.leftCharacter >= SokuLib::CHARACTER_RANDOM)
		return sendError(Trainer::ERROR_INVALID_LEFT_CHARACTER), false;
	if (packet.rightCharacter >= SokuLib::CHARACTER_RANDOM)
		return sendError(Trainer::ERROR_INVALID_RIGHT_CHARACTER), false;
	if (!isDeckValid(packet.leftCharacter, packet.leftDeck))
		return sendError(Trainer::ERROR_INVALID_LEFT_DECK), false;
	if (!isDeckValid(packet.rightCharacter, packet.rightDeck))
		return sendError(Trainer::ERROR_INVALID_RIGHT_DECK), false;
	return true;
}

static void handlePacket(const Trainer::Packet &packet, unsigned int size)
{
	if (!hello && packet.op != Trainer::OPCODE_HELLO)
		return sendError(Trainer::ERROR_HELLO_NOT_SENT);

	switch (packet.op) {
	case Trainer::OPCODE_HELLO:
		CHECK_PACKET_SIZE(Trainer::HelloPacket, size);
		if (packet.hello.magic != PACKET_HELLO_MAGIC_NUMBER) {
			sendError(Trainer::ERROR_INVALID_MAGIC);
			break;
		}
		::send(sock, reinterpret_cast<const char *>(&packet), size, 0);
		hello = true;
		return;
	case Trainer::OPCODE_GOODBYE:
		CHECK_PACKET_SIZE(Trainer::Opcode, size);
		sendOpcode(Trainer::OPCODE_OK);
		stop = true;
		return;
	case Trainer::OPCODE_SPEED:
		CHECK_PACKET_SIZE(Trainer::SpeedPacket, size);
		tps = packet.speed.ticksPerSecond;
		sendOpcode(Trainer::OPCODE_OK);
		return;
	case Trainer::OPCODE_START_GAME:
		CHECK_PACKET_SIZE(Trainer::StartGamePacket, size);
		if (!verifyStartData(packet.startGame))
			return;
		return startGame(packet.startGame);
	case Trainer::OPCODE_GAME_INPUTS:
		CHECK_PACKET_SIZE(Trainer::GameInputPacket, size);
		inputs.first  = packet.gameInput.left;
		inputs.second = packet.gameInput.right;
		inputsReceived = true;
		return;
	case Trainer::OPCODE_DISPLAY:
		CHECK_PACKET_SIZE(Trainer::DisplayPacket, size);
		swapDisplay(packet.display.enabled);
		sendOpcode(Trainer::OPCODE_OK);
		return;
	case Trainer::OPCODE_GAME_CANCEL:
		CHECK_PACKET_SIZE(Trainer::Opcode, size);
		sendOpcode(Trainer::OPCODE_OK);
		cancel = true;
		return;
	case Trainer::OPCODE_SOUND:
		CHECK_PACKET_SIZE(Trainer::SoundPacket, size);
		sendOpcode(Trainer::OPCODE_OK);
		reinterpret_cast<void (*)(int)>(0x43e200)(packet.sound.bgmVolume);
		reinterpret_cast<void (*)(int)>(0x43e230)(packet.sound.sfxVolume);
		return;
	case Trainer::OPCODE_SET_HEALTH:
		CHECK_PACKET_SIZE(Trainer::SetHealthPacket, size);
		if (SokuLib::sceneId != SokuLib::SCENE_BATTLE)
			return sendError(Trainer::ERROR_NOT_IN_GAME);
		sendOpcode(Trainer::OPCODE_OK);
		if (packet.setHealth.left > 10000 || packet.setHealth.right > 10000)
			return sendError(Trainer::ERROR_INVALID_HEALTH);
		SokuLib::getBattleMgr().leftCharacterManager.objectBase.hp = packet.setHealth.left;
		SokuLib::getBattleMgr().rightCharacterManager.objectBase.hp = packet.setHealth.right;
		return;
	case Trainer::OPCODE_SET_POSITION:
		CHECK_PACKET_SIZE(Trainer::SetPositionPacket, size);
		if (SokuLib::sceneId != SokuLib::SCENE_BATTLE)
			return sendError(Trainer::ERROR_NOT_IN_GAME);
		if (packet.setPosition.left.y < 0 || packet.setPosition.right.y < 0)
			return sendError(Trainer::ERROR_POSITION_OUT_OF_BOUND);
		if (packet.setPosition.left.x < 40 || packet.setPosition.right.x < 40)
			return sendError(Trainer::ERROR_POSITION_OUT_OF_BOUND);
		if (packet.setPosition.left.y > 1240 || packet.setPosition.right.y > 1240)
			return sendError(Trainer::ERROR_POSITION_OUT_OF_BOUND);
		SokuLib::getBattleMgr().leftCharacterManager.objectBase.position = packet.setPosition.left;
		SokuLib::getBattleMgr().rightCharacterManager.objectBase.position = packet.setPosition.right;
		sendOpcode(Trainer::OPCODE_OK);
		return;
	case Trainer::OPCODE_SET_WEATHER:
		CHECK_PACKET_SIZE(Trainer::SetWeatherPacket, size);
		if (SokuLib::sceneId != SokuLib::SCENE_BATTLE)
			return sendError(Trainer::ERROR_NOT_IN_GAME);
		if (packet.setWeather.timer > 999)
			return sendError(Trainer::ERROR_INVALID_WEATHER_TIME);
		if (packet.setWeather.weather > SokuLib::WEATHER_CLEAR)
			return sendError(Trainer::ERROR_INVALID_WEATHER);
		weatherTimer = packet.setWeather.timer;
		weather = packet.setWeather.weather;
		freezeWeather = packet.setWeather.freeze;
		SokuLib::displayedWeather = weather;
		SokuLib::activeWeather = SokuLib::WEATHER_CLEAR;
		SokuLib::weatherCounter = 999;
		setWeather = true;
		sendOpcode(Trainer::OPCODE_OK);
		return;
	case Trainer::OPCODE_ERROR:
	case Trainer::OPCODE_FAULT:
	case Trainer::OPCODE_GAME_FRAME:
	case Trainer::OPCODE_GAME_ENDED:
	default:
		return sendError(Trainer::ERROR_INVALID_OPCODE);
	}
}

static void threadLoop(void *)
{
	while (!stop) {
		Trainer::Packet packet;
		int received = ::recv(sock, reinterpret_cast<char *>(&packet), sizeof(packet), 0);

		if (received <= 0) {
			stop = true;
			SokuLib::changeScene(SokuLib::SCENE_SELECT);
			SokuLib::waitForSceneChange();
			return;
		}
		handlePacket(packet, received);
	}
}

int __fastcall CLogo_OnProcess(SokuLib::Logo *This) {
	int ret = (This->*s_origCLogo_OnProcess)();

	if (ret == SokuLib::SCENE_TITLE) {
		*(int **)0x008a0008 = nullptr;
		*(char *)0x89ffbd = true;
		if (__argc <= 1) {
			MessageBoxA(SokuLib::window, "No port provided. Please provide a port in the command line.", "Port not given", MB_ICONERROR);
			return -1;
		}
		char *arg = __argv[__argc - 1];
		WSADATA WSAData;
		struct sockaddr_in serv_addr = {};
		char *end;
		long port = strtol(arg, &end, 0);
		char *s = nullptr;

		SetWindowTextA(SokuLib::window, ("Game on port " + std::to_string(port)).c_str());
		if (*end) {
			MessageBoxA(
				SokuLib::window,
				("Port provided as argument #" + std::to_string(__argc) + " (" + std::string(arg) +
				") is not a valid number.\nPlease provide a valid port in the command line.").c_str(),
				"Port invalid",
				MB_ICONERROR
			);
			return -1;
		}
		if (port < 0 || port > 65535) {
			MessageBoxA(
				SokuLib::window,
				("Port provided #" + std::to_string(__argc) + " (" + std::string(arg) +
				 ") is not a valid port (not in range 0-65535).\nPlease provide a valid port in the command line.").c_str(),
				"Port invalid",
				MB_ICONERROR
			);
			return -1;
		}
		if (WSAStartup(MAKEWORD(2, 0), &WSAData) < 0) {
			FormatMessageA(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr,
				WSAGetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPSTR)&s,
				0,
				nullptr
			);
			MessageBoxA(SokuLib::window, (std::string("WSAStartup failed\n\nWSAGetLastError(): ") + std::to_string(WSAGetLastError()) + ": " + s).c_str(), "Init failure", MB_ICONERROR);
			return -1;
		}

		/* create the socket */
		sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET) {
			FormatMessageA(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr,
				WSAGetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPSTR)&s,
				0,
				nullptr
			);
			MessageBoxA(SokuLib::window, (std::string("Creating socket failed\n\nWSAGetLastError(): ") + std::to_string(WSAGetLastError()) + ": " + s).c_str(), "Init failure", MB_ICONERROR);
			return -1;
		}

		/* fill in the structure */
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(port);
		serv_addr.sin_addr.S_un.S_un_b.s_b1 = 127;
		serv_addr.sin_addr.S_un.S_un_b.s_b2 = 0;
		serv_addr.sin_addr.S_un.S_un_b.s_b3 = 0;
		serv_addr.sin_addr.S_un.S_un_b.s_b4 = 1;

		/* connect the socket */
		if (::connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
			FormatMessageA(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr,
				WSAGetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPSTR)&s,
				0,
				nullptr
			);
			MessageBoxA(SokuLib::window, (
				std::string("Cannot connect to ") + inet_ntoa(serv_addr.sin_addr) + " on port " + std::to_string(port) +
				".\nPort gotten from command line argument #" + std::to_string(__argc) +
				" (" + arg + ").\n\nWSAGetLastError(): " + std::to_string(WSAGetLastError()) + ": " + s
			).c_str(), "Connection failure", MB_ICONERROR);
			return -1;
		}
		_beginthread(threadLoop, 0, nullptr);
	}
	return ret;
}

int __fastcall CSelect_OnProcess(SokuLib::Select *This) {
	puts("CSelect_OnProcess");
	int ret = (This->*s_origCSelect_OnProcess)();

	if (stop)
		return -1;
	//if (ret == SokuLib::SCENE_LOADING)
	//	return SokuLib::SCENE_LOADING;
	//return SokuLib::SCENE_SELECT;
	if (ret == SokuLib::SCENE_LOADING) {
		for (int i = 0; i < 20; i++)
			SokuLib::leftPlayerInfo.effectiveDeck[i] = decks[i];
		for (int i = 0; i < 20; i++)
			SokuLib::rightPlayerInfo.effectiveDeck[i] = decks[20 + i];
	}
	startRequested &= ret == SokuLib::SCENE_SELECT;
	printf("Return %i\n", ret);
	return ret;
}

int __fastcall CBattle_OnProcess(SokuLib::Battle *This) {
	puts("CBattle_OnProcess");
	auto &battle = SokuLib::getBattleMgr();
	Trainer::GameEndedPacket endPacket;

	if (setWeather || freezeWeather) {
		if (weather == SokuLib::WEATHER_CLEAR)
			SokuLib::weatherCounter = -1;
		else if (SokuLib::activeWeather == weather) {
			SokuLib::weatherCounter = weatherTimer;
			setWeather = false;
		}
	}
	counter += tps / 60.f;
	while (counter >= 1) {
		puts("Game frame");
		isFrame = true;
		int ret = (This->*s_origCBattle_OnProcess)();
		isFrame = false;
		puts("After frame");
		if (!gameFinished) {
			size_t allocSize = sizeof(Trainer::GameFramePacket) + (battle.leftCharacterManager.objects.list.size + battle.rightCharacterManager.objects.list.size) * sizeof(Trainer::Object);
			char *buffer = new char[allocSize];
			Trainer::GameFramePacket *packet = reinterpret_cast<Trainer::GameFramePacket *>(buffer);
			int current = 0;

			fillState(battle.leftCharacterManager, battle.rightCharacterManager, packet->leftState);
			fillState(battle.rightCharacterManager, battle.leftCharacterManager, packet->rightState);
			packet->op = Trainer::OPCODE_GAME_FRAME;
			packet->weatherTimer = SokuLib::weatherCounter;
			packet->displayedWeather = SokuLib::displayedWeather;
			if (SokuLib::activeWeather != SokuLib::WEATHER_CLEAR &&
			    SokuLib::displayedWeather == SokuLib::WEATHER_AURORA)
				packet->activeWeather = SokuLib::WEATHER_AURORA;
			else
				packet->activeWeather = SokuLib::activeWeather;
			for (auto obj : battle.leftCharacterManager.objects.list.vector())
				fillState(*obj, battle.leftCharacterManager, battle.rightCharacterManager, packet->objects[current++]);
			for (auto obj : battle.rightCharacterManager.objects.list.vector())
				fillState(*obj, battle.rightCharacterManager, battle.leftCharacterManager, packet->objects[current++]);

			inputsReceived = false;
			send(sock, buffer, allocSize, 0);
			delete[] buffer;
			gameFinished |= battle.leftCharacterManager.score == 2 || battle.rightCharacterManager.score == 2;
			while (!cancel && !gameFinished && !stop && !inputsReceived);
		}
		if (ret != SokuLib::SCENE_BATTLE || cancel) {
			gameFinished = false;
			endPacket.op = Trainer::OPCODE_GAME_ENDED;
			endPacket.winner = 0 + (battle.leftCharacterManager.score == 2) + (battle.rightCharacterManager.score == 2) * 2;
			endPacket.leftScore = battle.leftCharacterManager.score;
			endPacket.rightScore = battle.rightCharacterManager.score;
			send(sock, reinterpret_cast<const char *>(&endPacket), sizeof(endPacket), 0);
			puts("Return title");
			return SokuLib::SCENE_TITLE;
		}
		if (stop)
			return -1;
		counter--;
	}
	puts("Return battle");
	return SokuLib::SCENE_BATTLE;
}

// 設定ロード
void LoadSettings(LPCSTR profilePath) {
	// 自動シャットダウン
	//FILE *_;
	//AllocConsole();
	//freopen_s(&_, "CONOUT$", "w", stdout);
	//freopen_s(&_, "CONOUT$", "w", stderr);
}

extern "C" __declspec(dllexport) bool CheckVersion(const BYTE hash[16]) {
	return ::memcmp(SokuLib::targetHash, hash, sizeof SokuLib::targetHash) == 0;
}

#define PAYLOAD_ADDRESS_GET_INPUTS 0x40A45E
#define PAYLOAD_NEXT_INSTR_GET_INPUTS (PAYLOAD_ADDRESS_GET_INPUTS + 4)

extern "C" __declspec(dllexport) bool Initialize(HMODULE hMyModule, HMODULE hParentModule) {
	char profilePath[1024 + MAX_PATH];

	GetModuleFileName(hMyModule, profilePath, 1024);
	PathRemoveFileSpec(profilePath);
	PathAppend(profilePath, "SokuAITrainer.ini");
	LoadSettings(profilePath);

	DWORD old;
	::VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old);
	s_origCLogo_OnProcess   = SokuLib::TamperDword(&SokuLib::VTable_Logo.onProcess,   CLogo_OnProcess);
	s_origCBattle_OnProcess = SokuLib::TamperDword(&SokuLib::VTable_Battle.onProcess, CBattle_OnProcess);
	s_origCSelect_OnProcess = SokuLib::TamperDword(&SokuLib::VTable_Select.onProcess, CSelect_OnProcess);
	SokuLib::TamperDword(&SokuLib::VTable_Logo.onRender, dummyFunction);
	::VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, old, &old);

	swapDisplay(false);
	VirtualProtect((PVOID)TEXT_SECTION_OFFSET, TEXT_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old);
	int newOffset = (int)KeymapManagerSetInputs - PAYLOAD_NEXT_INSTR_GET_INPUTS;
	s_origKeymapManager_SetInputs = SokuLib::union_cast<void (SokuLib::KeymapManager::*)()>(*(int *)PAYLOAD_ADDRESS_GET_INPUTS + PAYLOAD_NEXT_INSTR_GET_INPUTS);
	*(int *)PAYLOAD_ADDRESS_GET_INPUTS = newOffset;
	*(int *)0x47d7a0 = 0xC3;
	VirtualProtect((PVOID)TEXT_SECTION_OFFSET, TEXT_SECTION_SIZE, old, &old);
	::FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
	return true;
}

extern "C" int APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved) {
	if (fdwReason == DLL_PROCESS_DETACH)
		stop = true;
	return TRUE;
}
