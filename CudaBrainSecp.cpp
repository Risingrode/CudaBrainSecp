#include <cstring>
#include <cmath>
#include <algorithm>
#include <iterator>
#include <iostream>
#include <cassert>
#include <pthread.h>
#include <fstream>
#include "GPU/GPUSecp.h"
#include "CPU/SECP256k1.h"
#include "CPU/HashMerge.cpp"
#include "CPU/Combo.cpp"
#include "CPU/BIP39.h"
#include <sys/resource.h>
#include <chrono>
#include <sstream>

long getFileContent(std::string fileName, std::vector<std::string> &vecOfStrs) {
	long totalSizeBytes = 0;

	std::ifstream in(fileName.c_str());
	if (!in)
	{
		std::cerr << "Can not open the File : " << fileName << std::endl;
		return 0;
	}
	std::string str;
	while (std::getline(in, str))
	{
		vecOfStrs.push_back(str);
		totalSizeBytes += str.size();
	}
	
	in.close();
	return totalSizeBytes;
}

int getBookWordCount(std::string inputName) {
	std::vector<std::string> bookVector;
	getFileContent(inputName, bookVector);
	return bookVector.size();
}

uint8_t* loadInputBook(std::string inputName, int wordMaxLength) {
	std::cout << "loadInputBook " << inputName << " started" << std::endl;
	std::vector<std::string> bookVector;

	getFileContent(inputName, bookVector);
	int bookWordCount = bookVector.size();
	
	uint8_t* inputBookCPU = (uint8_t*)malloc(bookWordCount * wordMaxLength);
	memset(inputBookCPU, 0, bookWordCount * wordMaxLength);

	int idx = 0;
	for (std::string &line : bookVector)
	{
		inputBookCPU[idx] = (uint8_t)line.length();
		memcpy(inputBookCPU + idx + 1, line.c_str(), line.length());
		idx += wordMaxLength;
	}

	std::cout << "loadInputBook " << inputName << " finished! wordCount: " << bookWordCount << std::endl;
	return inputBookCPU;
}

long loadInputHash(uint64_t *&inputHashBufferCPU) {
    std::cout << "Loading hash buffer from file: " << NAME_HASH_BUFFER << std::endl;

    FILE *fileSortedHash = fopen(NAME_HASH_BUFFER, "rb");
    if (fileSortedHash == NULL)
    {
        printf("Error: not able to open input file: %s\n", NAME_HASH_BUFFER);
        exit(1);
    }

    fseek(fileSortedHash, 0, SEEK_END);
    long hashBufferSizeBytes = ftell(fileSortedHash);
    long hashCount = hashBufferSizeBytes / SIZE_LONG;
    rewind(fileSortedHash);

    if (hashBufferSizeBytes % SIZE_LONG != 0) {
        printf("ERROR - Hash buffer size (%lu) is not a multiple of %d bytes.\n", hashBufferSizeBytes, SIZE_LONG);
        exit(-1);
    }

    inputHashBufferCPU = new uint64_t[hashCount];
    size_t size = fread(inputHashBufferCPU, 1, hashBufferSizeBytes, fileSortedHash);
    fclose(fileSortedHash);

    std::cout << "loadInputHash " << NAME_HASH_BUFFER << " finished!" << std::endl;
    std::cout << "hashCount: " << hashCount << ", hashBufferSizeBytes: " << hashBufferSizeBytes << std::endl;
    return hashCount;
}

void loadGTable(uint8_t *gTableX, uint8_t *gTableY) {
	std::cout << "loadGTable started" << std::endl;

	Secp256K1 *secp = new Secp256K1();
	secp->Init();

	for (int i = 0; i < NUM_GTABLE_CHUNK; i++)
	{
		for (int j = 0; j < NUM_GTABLE_VALUE - 1; j++)
		{
			int element = (i * NUM_GTABLE_VALUE) + j;
			Point p = secp->GTable[element];
			for (int b = 0; b < 32; b++) {
				gTableX[(element * SIZE_GTABLE_POINT) + b] = p.x.GetByte64(b);
				gTableY[(element * SIZE_GTABLE_POINT) + b] = p.y.GetByte64(b);
			}
		}
	}

	std::cout << "loadGTable finished!" << std::endl;
}

void startSecp256k1ModeBooks(uint8_t * gTableXCPU, uint8_t * gTableYCPU, uint64_t * inputHashBufferCPU, int countInputHash) {

	printf("CudaBrainSecp.ModeBooks Starting \n");

	int countPrime = getBookWordCount(NAME_INPUT_PRIME);
	int countAffix = getBookWordCount(NAME_INPUT_AFFIX);

	uint8_t* inputBookPrimeCPU = loadInputBook(NAME_INPUT_PRIME, MAX_LEN_WORD_PRIME);
	uint8_t* inputBookAffixCPU = loadInputBook(NAME_INPUT_AFFIX, MAX_LEN_WORD_AFFIX);

    GPUSecp *gpuSecp = new GPUSecp(
        countPrime,
        countAffix,
        gTableXCPU,
        gTableYCPU,
        inputBookPrimeCPU,
        inputBookAffixCPU,
        inputHashBufferCPU,
        countInputHash,
        0
    );

	long timeTotal = 0;
    long totalCount = (countAffix * countPrime);
    int maxIteration = countAffix / COUNT_CUDA_THREADS;

	for (int iter = 0; iter < maxIteration; iter++) {
		const auto clockIter1 = std::chrono::system_clock::now();
		gpuSecp->doIterationSecp256k1Books(iter);
		const auto clockIter2 = std::chrono::system_clock::now();
		gpuSecp->doPrintOutput();

		long timeIter1 = std::chrono::duration_cast<std::chrono::milliseconds>(clockIter1.time_since_epoch()).count();
		long timeIter2 = std::chrono::duration_cast<std::chrono::milliseconds>(clockIter2.time_since_epoch()).count();
		long iterationDuration = (timeIter2 - timeIter1);
		timeTotal += iterationDuration;

		printf("CudaBrainSecp.ModeBooks Iteration: %d, time: %ld \n", iter, iterationDuration);
	}

	printf("CudaBrainSecp.ModeBooks Complete \n");

	printf("Finished %d iterations in %ld milliseconds \n", maxIteration, timeTotal);

	printf("Total Seed Count: %lu \n", totalCount);

	printf("Seeds Per Second: %0.2lf Million\n", totalCount / (double)(timeTotal * 1000));
}

void startSecp256k1ModeCombo(uint8_t * gTableXCPU, uint8_t * gTableYCPU, uint64_t * inputHashBufferCPU, int countInputHash) {

	printf("CudaBrainSecp.ModeCombo Starting \n");

	if (SIZE_COMBO_MULTI < 4 || SIZE_COMBO_MULTI > 8) {
		printf("Currently supported combination sizes are 4, 5, 6, 7 and 8. \n");
		printf("If you wish you can easily add logic for larger combination buffers. \n");
		printf("Simply edit Combo->adjustComboBuffer, GPUHash->_FindComboStart, GPUHash->_SHA256Combo functions. \n");
		exit(-1);
	}

    GPUSecp *gpuSecp = new GPUSecp(
        0,
        0,
        gTableXCPU,
        gTableYCPU,
        NULL,
        NULL,
        inputHashBufferCPU,
        countInputHash,
        0
    );

	long timeTotal = 0;
	long totalComboCount = 1;

	for (int i = 0; i < SIZE_COMBO_MULTI; i++) {
		totalComboCount = totalComboCount * COUNT_COMBO_SYMBOLS;
	}

	long comboPerIteration = (COUNT_CUDA_THREADS * COUNT_COMBO_SYMBOLS * COUNT_COMBO_SYMBOLS);
	long maxIteration = 1 + (totalComboCount / comboPerIteration);
	int8_t comboCPU[SIZE_COMBO_MULTI] = {};

	printf("CudaBrainSecp.ModeCombo maxIteration: %ld \n", maxIteration);
	printf("CudaBrainSecp.ModeCombo totalComboCount: %ld \n", totalComboCount);
	printf("CudaBrainSecp.ModeCombo comboPerIteration: %ld \n", comboPerIteration);

	for (int iter = 0; iter < maxIteration; iter++) {
		printf("CudaBrainSecp.ModeCombo Combination: [");
		for (int i = 0; i < SIZE_COMBO_MULTI; i++) {
			printf("%d ", comboCPU[i]);
		}
		printf("]\n");

		const auto clockIter1 = std::chrono::system_clock::now();
		gpuSecp->doIterationSecp256k1Combo(comboCPU);
		const auto clockIter2 = std::chrono::system_clock::now();
		gpuSecp->doPrintOutput();

		long timeIter1 = std::chrono::duration_cast<std::chrono::milliseconds>(clockIter1.time_since_epoch()).count();
		long timeIter2 = std::chrono::duration_cast<std::chrono::milliseconds>(clockIter2.time_since_epoch()).count();
		long iterationDuration = (timeIter2 - timeIter1);
		timeTotal += iterationDuration;

		printf("CudaBrainSecp.ModeCombo Iteration: %d, time: %ld \n", iter, iterationDuration);

		adjustComboBuffer(comboCPU, COUNT_CUDA_THREADS);
	}

	printf("CudaBrainSecp.ModeCombo Complete \n");

	printf("Finished %ld iterations in %ld milliseconds \n", maxIteration, timeTotal);

	printf("Total Seed Count: %lu \n", totalComboCount);

	printf("Seeds Per Second: %0.2lf Million\n", totalComboCount / (double)(timeTotal * 1000));
}

// ------------------------ BIP39/BIP32 Mode (CPU derives privkeys, GPU multiplies+matches) ------------------------

static bool parseArgKV(const std::string &arg, const char* key, std::string &out) {
    std::string k = std::string("--") + key + "=";
    if (arg.rfind(k, 0) == 0) { out = arg.substr(k.size()); return true; }
    return false;
}

void startBIP39Mode(uint8_t * gTableXCPU, uint8_t * gTableYCPU, uint64_t * inputHashBufferCPU, int countInputHash,
                    int argc, char **argv) {
    printf("CudaBrainSecp.BIP39 Starting \n");

    // Defaults
    std::string mnemoFile = "mnemonics.txt"; // one mnemonic per line (ASCII/pre-normalized)
    std::string passphrase = "";
    std::string pathStr = ""; // derive from addr mode if not set
    uint32_t rangeStart = 0; uint32_t rangeCount = 1; // 默认只取索引0
    int addrMode = 0; // 0=P2PKH(44), 1=P2SH-P2WPKH(49), 2=P2WPKH(84)
    std::string dictFile = "";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        std::string v;
        if (parseArgKV(a, "mnemonics", v)) mnemoFile = v;
        else if (parseArgKV(a, "pass", v)) passphrase = v;
        else if (parseArgKV(a, "path", v)) pathStr = v;
        else if (parseArgKV(a, "addr", v)) {
            if (v == "p2pkh" || v == "44") addrMode = 0;
            else if (v == "p2sh-p2wpkh" || v == "49" || v == "p2sh") addrMode = 1;
            else if (v == "p2wpkh" || v == "84" || v == "bech32") addrMode = 2;
        }
        else if (parseArgKV(a, "dict", v)) dictFile = v;
        else if (parseArgKV(a, "range", v)) {
            size_t c = v.find(":");
            if (c != std::string::npos) { rangeStart = std::stoul(v.substr(0, c)); rangeCount = std::stoul(v.substr(c+1)); }
        }
    }

    std::ifstream in(mnemoFile.c_str());
    if (!in) { fprintf(stderr, "BIP39: cannot open mnemonics file: %s\n", mnemoFile.c_str()); exit(1); }
    std::vector<std::string> mnemonics; std::string line;
    while (std::getline(in, line)) { if (!line.empty()) mnemonics.push_back(line); }
    in.close();
    if (mnemonics.empty()) { fprintf(stderr, "BIP39: mnemonics file is empty\n"); exit(1); }

    // 如果包含 ? ，用字典展开（最多支持缺失3词）
    // 不在此处做预展开，交由后续流式阶段一边生成一边过滤与派生

    if (pathStr.empty()) {
        if (addrMode == 0) pathStr = "m/44'/0'/0'/0/0";
        else if (addrMode == 1) pathStr = "m/49'/0'/0'/0/0";
        else pathStr = "m/84'/0'/0'/0/0";
    }

    std::vector<uint32_t> path;
    if (!BIP39::ParsePath(pathStr, path)) { fprintf(stderr, "BIP39: invalid path: %s\n", pathStr.c_str()); exit(1); }

    // Streaming batches: build first batch then reuse GPU for subsequent batches
    int BATCH_MNEMO = 20000; // 每批最多 2 万条助记词（可根据显存/CPU并发调整）
    std::string vbatch; if (parseArgKV(std::string(argc>0?argv[0]:""), "batch", vbatch)) {}
    for (int i = 1; i < argc; ++i) { std::string aa = argv[i]; if (parseArgKV(aa, "batch", vbatch)) { BATCH_MNEMO = std::max(1000, std::stoi(vbatch)); } }
    std::vector<std::string> batchMnemo; batchMnemo.reserve(BATCH_MNEMO);
    auto processBatch = [&](GPUSecp *&gpuSecp){
        if (batchMnemo.empty()) return;
        std::vector<uint8_t> privList;
        if (!BIP39::BuildPrivListFromMnemonics(batchMnemo, passphrase, path, rangeStart, rangeCount, privList)) {
            batchMnemo.clear();
            return;
        }
        int countPriv = (int)(privList.size() / SIZE_PRIV_KEY);
        if (countPriv <= 0) { batchMnemo.clear(); return; }
        if (!gpuSecp) {
            gpuSecp = new GPUSecp(
                countPriv,
                privList.data(),
                gTableXCPU,
                gTableYCPU,
                inputHashBufferCPU,
                countInputHash,
                addrMode
            );
        } else {
            gpuSecp->setPrivList(privList.data(), countPriv);
        }
        int maxIteration = 1 + ((countPriv - 1) / COUNT_CUDA_THREADS);
        for (int iter = 0; iter < maxIteration; iter++) {
            const auto clockIter1 = std::chrono::system_clock::now();
            gpuSecp->doIterationSecp256k1PrivList(iter);
            const auto clockIter2 = std::chrono::system_clock::now();
            gpuSecp->doPrintOutput();
            long t1 = std::chrono::duration_cast<std::chrono::milliseconds>(clockIter1.time_since_epoch()).count();
            long t2 = std::chrono::duration_cast<std::chrono::milliseconds>(clockIter2.time_since_epoch()).count();
            printf("CudaBrainSecp.BIP39 Iteration: %d, time: %ld \n", iter, (t2 - t1));
        }
        batchMnemo.clear();
    };

    GPUSecp *gpuSecp = nullptr;

    // 如果没有 ?，直接把整份 mnemonics 以批次送入
    auto pushPlainList = [&](const std::vector<std::string>& list){
        for (const auto &m : list) { batchMnemo.push_back(m); if ((int)batchMnemo.size() >= BATCH_MNEMO) processBatch(gpuSecp); }
        processBatch(gpuSecp);
    };

    bool hasWildcard = false;
    for (auto &s : mnemonics) { if (s.find('?') != std::string::npos) { hasWildcard = true; break; } }
    if (!hasWildcard) {
        pushPlainList(mnemonics);
    } else {
        // 对含 ? 的模板进行流式展开 + checksum 过滤
        std::vector<std::string> dict;
        if (!BIP39::LoadWordlist("CPU/bip39_english.txt", dict)) {
            fprintf(stderr, "BIP39: failed to load built-in English wordlist (CPU/bip39_english.txt)\n");
            exit(1);
        }
        std::unordered_map<std::string,int> wlIndex; wlIndex.reserve(dict.size()*2);
        for (size_t i=0;i<dict.size();++i) wlIndex[dict[i]] = (int)i;

        for (const std::string &tmpl : mnemonics) {
            std::vector<std::string> words; words.reserve(24);
            std::string tmp; std::istringstream iss(tmpl); while (iss >> tmp) words.push_back(tmp);
            std::vector<int> qpos; for (size_t i=0;i<words.size();++i) if (words[i]=="?") qpos.push_back((int)i);
            if (qpos.empty()) { batchMnemo.push_back(tmpl); if ((int)batchMnemo.size() >= BATCH_MNEMO) processBatch(gpuSecp); continue; }
            if (qpos.size() > 3) { fprintf(stderr, "BIP39: too many '?' (%zu), max supported is 3.\n", qpos.size()); exit(1); }
            if (qpos.size() == 1) {
                for (const auto &a : dict) { auto ww=words; ww[qpos[0]]=a; if (BIP39::IsValidMnemonicWithWordlist(ww, dict, wlIndex)) { std::ostringstream os; for(size_t i=0;i<ww.size();++i){ if(i) os<<' '; os<<ww[i]; } batchMnemo.push_back(os.str()); if ((int)batchMnemo.size() >= BATCH_MNEMO) processBatch(gpuSecp); } }
            } else if (qpos.size() == 2) {
                for (const auto &a : dict) { for (const auto &b : dict) { auto ww=words; ww[qpos[0]]=a; ww[qpos[1]]=b; if (BIP39::IsValidMnemonicWithWordlist(ww, dict, wlIndex)) { std::ostringstream os; for(size_t i=0;i<ww.size();++i){ if(i) os<<' '; os<<ww[i]; } batchMnemo.push_back(os.str()); if ((int)batchMnemo.size() >= BATCH_MNEMO) processBatch(gpuSecp); } } }
            } else {
                for (const auto &a : dict) { for (const auto &b : dict) { for (const auto &c : dict) { auto ww=words; ww[qpos[0]]=a; ww[qpos[1]]=b; ww[qpos[2]]=c; if (BIP39::IsValidMnemonicWithWordlist(ww, dict, wlIndex)) { std::ostringstream os; for(size_t i=0;i<ww.size();++i){ if(i) os<<' '; os<<ww[i]; } batchMnemo.push_back(os.str()); if ((int)batchMnemo.size() >= BATCH_MNEMO) processBatch(gpuSecp); } } } }
            }
        }
        processBatch(gpuSecp);
    }
    printf("CudaBrainSecp.BIP39 Complete \n");
}

void increaseStackSizeCPU() {
	const rlim_t cpuStackSize = SIZE_CPU_STACK;
	struct rlimit rl;
	int result;

	printf("Increasing Stack Size to %lu \n", cpuStackSize);

	result = getrlimit(RLIMIT_STACK, &rl);
	if (result == 0)
	{
		if (rl.rlim_cur < cpuStackSize)
		{
			rl.rlim_cur = cpuStackSize;
			result = setrlimit(RLIMIT_STACK, &rl);
			if (result != 0)
			{
				fprintf(stderr, "setrlimit returned result = %d\n", result);
			}
		}
	}
}

int main(int argc, char **argv) {
	printf("CudaBrainSecp Starting \n");

	increaseStackSizeCPU();

	mergeHashes(NAME_HASH_FOLDER, NAME_HASH_BUFFER);

	uint8_t* gTableXCPU = new uint8_t[COUNT_GTABLE_POINTS * SIZE_GTABLE_POINT];
	uint8_t* gTableYCPU = new uint8_t[COUNT_GTABLE_POINTS * SIZE_GTABLE_POINT];

	loadGTable(gTableXCPU, gTableYCPU);

	uint64_t* inputHashBufferCPU = NULL;
	long countInputHash = loadInputHash(inputHashBufferCPU);


	bool bip39 = false;
	for (int i = 1; i < argc; ++i) { if (std::string(argv[i]) == "--bip39") { bip39 = true; break; } }
	if (bip39) {
		startBIP39Mode(gTableXCPU, gTableYCPU, inputHashBufferCPU, (int)countInputHash, argc, argv);
	} else {
		startSecp256k1ModeBooks(gTableXCPU, gTableYCPU, inputHashBufferCPU, (int)countInputHash);
	}
	
	//startSecp256k1ModeCombo(gTableXCPU, gTableYCPU, inputHashBufferCPU);

	free(gTableXCPU);
	free(gTableYCPU);
	delete[] inputHashBufferCPU;

	printf("CudaBrainSecp Complete \n");
	return 0;
}
