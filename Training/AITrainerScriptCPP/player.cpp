//
// Created by PinkySmile on 26/05/2021.
//

#include <random>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include "GameManager.hpp"
#include "GeneticAI.hpp"

using namespace Trainer;

std::mt19937 random;
int main(int argc, char **argv)
{
	//for (int i = 1; i < argc; i++) {
	//	for (int j = 0; argv[i][j]; j++)
	//		printf("%i %c\n", (unsigned char)argv[i][j], argv[i][j]);
	//	puts("");
	//}
	if (argc < 6) {
		printf("Usage: %s <game_path> <port> <input_delay> <timeout> <middle_layer_size> <gene_count> <ai_file> [<SWRSToys.ini>]\n", argv[0]);
		system("pause");
		return EXIT_FAILURE;
	}
	random.seed(time(nullptr));
	//srand(time(nullptr));

	std::string client = argv[1];
	printf("Client: %s\n", client.c_str());
	unsigned short port = std::stoul(argv[2]);
	printf("Port: %i\n", port);
	unsigned inputDelay = std::stoul(argv[3]);
	printf("Input delay: %i\n", inputDelay);
	unsigned timeout = std::stoul(argv[4]);
	printf("Timeout: %i\n", inputDelay);
	int NEURON_COUNT = std::stoul(argv[5]);
	printf("Neuron count: %i\n", NEURON_COUNT);
	int GENES_COUNT = std::stoul(argv[6]);
	printf("Gene count: %i\n", GENES_COUNT);
	std::string aiPath = argv[7];
	printf("Ai path: %s\n", aiPath.c_str());
	const char* ini = argc == 9 ? argv[8] : nullptr;
	printf("Ini: %s\n", ini ? ini : "null");
	GameManager gameManager{client, port, 60, true, 20, 20, ini};
	BaseAI *ai;
	BaseAI irrel{SokuLib::CHARACTER_REMILIA, 0};
	GameInstance::StartGameParams params;

	ai = new GeneticAI(28, 28, NEURON_COUNT, GENES_COUNT, aiPath);
	params.left = irrel.getParams();
	params.right = ai->getParams();
	params.stage = 13;
	params.music = 13;

	gameManager.leftAi = &irrel;
	gameManager.rightAi = ai;
	try {
		while (true)
			gameManager.runOnce(params, -1, inputDelay,false,true);
	} catch (std::exception &e) {
		puts(e.what());
	}
}