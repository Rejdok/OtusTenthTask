#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <ctime>
#include <fstream>
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include <chrono>
#include <atomic>
#include <tuple>

enum class state { inputStatcSizeBlock, inputDynamicSizeBlock, outputBlock, terminate };

using ComandBlockT = std::vector<std::string>;
using CommandQueueT = std::queue<ComandBlockT>;
using FileMutexT = std::mutex;
using QueueMutexT = std::mutex;

class OFThread{
public:
	OFThread(std::shared_ptr<CommandQueueT> const& commandsQueue, std::shared_ptr<QueueMutexT>& queueMutex,
	std::shared_ptr<FileMutexT>& fileCreateMutex,
	std::shared_ptr<std::atomic_bool>& isInputStreamOver) :commandsQueue(commandsQueue),queueMutex(queueMutex),
		fileCreateMutex(fileCreateMutex),isInputStreamOver(isInputStreamOver) {};
	OFThread(OFThread&& ofthread) = default;
	OFThread(OFThread& ofthread) = default;
	bool fileExist(std::string& fileName) {
		std::ifstream fileExistsCheck;
		fileExistsCheck.open(fileName);
		auto rez = fileExistsCheck.good();
		fileExistsCheck.close();
		return rez;
	}
	void writeBlockToFile(ComandBlockT& commandsBlock) {
		countOfBlocks++;
		bool isOFileCreated = false;
		std::ofstream outputFile;
		size_t countOfTries = 0;
		std::string fileName = std::string("bulk") + commandsBlock[0] + "_";
		while (!isOFileCreated) {
			std::string fName = fileName + std::to_string(countOfTries);
			fileCreateMutex->lock();
			bool isFileExist = fileExist(fName);
			if (!isFileExist) {
				outputFile.open(fileName + std::to_string(countOfTries));
				if (outputFile.is_open()) {
					isOFileCreated = true;
				}
			}
			fileCreateMutex->unlock();
			countOfTries++;
		}
		outputFile << "bulk: ";
		for (size_t i = 1; i < commandsBlock.size(); i++) {
			countOfCommands++;
			outputFile << commandsBlock[i];
			if (i != commandsBlock.size() - 1) {
				outputFile << ", ";
			}
		}
		outputFile << std::endl;
		outputFile.close();
	}

	void outputToFile() {
		bool shouldTerminate = false;
		while (true) {
			bool isLockMutexSucsesseful = queueMutex->try_lock();
			bool isQueueEmpty = false;
			if(isLockMutexSucsesseful)
				isQueueEmpty = commandsQueue->empty();
			if (isLockMutexSucsesseful && !isQueueEmpty) {
				if (commandsQueue->empty() && *isInputStreamOver) {
					shouldTerminate = true;
				}
				auto commandsBlock =std::move(commandsQueue->front());
				commandsQueue->pop();
				queueMutex->unlock();
				if (commandsBlock.size() > 1) {
					writeBlockToFile(commandsBlock);
				}
			}
			else {
				if (isLockMutexSucsesseful) {
					queueMutex->unlock();
				}
				if (*isInputStreamOver && isQueueEmpty) {
					shouldTerminate = true;
				}
				else {
					std::this_thread::sleep_for(std::chrono::microseconds(1));
					totalSleepTime += 1;
				}
			}
			if (shouldTerminate) {
				return;
			}
		}
	}
	auto getCountOfBlocksAndCommands() {
		return std::make_tuple(countOfBlocks, countOfCommands,totalSleepTime);
	}
private:
	size_t countOfBlocks = 0;
	size_t countOfCommands = 0;
	size_t totalSleepTime = 0;
	std::shared_ptr<QueueMutexT> queueMutex;
	std::shared_ptr<FileMutexT> fileCreateMutex;
	std::shared_ptr<std::atomic_bool> isInputStreamOver;
	std::shared_ptr<CommandQueueT> commandsQueue;
};

class CommandsProcessor {
public:
	CommandsProcessor(size_t countsOfThreads) {
		isInputStreamOver = false;
		commandsQueue = std::make_shared<CommandQueueT>();
		queueMutex = std::make_shared<QueueMutexT>();
		fileCreateMutex = std::make_shared<FileMutexT>();
		isInputStreamOver= std::make_shared<std::atomic_bool>();
		workers.reserve(countsOfThreads);
		IOFstreams.reserve(countsOfThreads);
		for (size_t i = 0; i < countsOfThreads; i++) {
			IOFstreams.emplace_back(commandsQueue, queueMutex, fileCreateMutex, isInputStreamOver);
			workers.emplace_back(&OFThread::outputToFile, &IOFstreams[i]);
		}
		for (size_t i = 0; i < countsOfThreads; i++) {
			
		}
	}
	auto& inputNewCommand(std::string &command) {
		auto& a = std::getline(std::cin, command);
		if (!a) {
			if (currentState == state::inputStatcSizeBlock) {
				stateQueue.push(state::outputBlock);
				stateQueue.push(state::terminate);
			}
			stateQueue.push(state::terminate);
		}
		countOfLines++;
		return a;
	}

	void inputSaticSizeBlock(int const & countOfCommandsInBlock, ComandBlockT &commandsInBlock) {
		std::string currentCommand;
		commandsInBlock.reserve(countOfCommandsInBlock);
		for (int i = 0; i < countOfCommandsInBlock&&inputNewCommand(currentCommand); i++) {
			if (i == 0) {
				auto firstCommandTime = std::time(NULL);
				commandsInBlock.emplace_back(std::to_string(firstCommandTime));
			}
			if (currentCommand == startDynamicBlock) {
				stateQueue.push(state::outputBlock);
				stateQueue.push(state::inputDynamicSizeBlock);
				return;
			}
			commandsInBlock.emplace_back(currentCommand);
		}
		stateQueue.push(state::outputBlock);
		stateQueue.push(state::inputStatcSizeBlock);
	}

	void outputToConsole(ComandBlockT const &commandsInBlock) {
		if (commandsInBlock.size()>1) {
			std::cout << "bulk: ";
			countOfBlocks++;
			for (size_t i = 1; i < commandsInBlock.size(); i++) {
				countOfCommands++;
				std::cout << commandsInBlock[i];
				if (i != commandsInBlock.size() - 1) {
					std::cout << ", ";
				}
			}
			std::cout << std::endl;
		}
	}

	void outputBlock(ComandBlockT const &commandsInBlock) {
		queueMutex->lock();
		commandsQueue->push(commandsInBlock);
		queueMutex->unlock();
		outputToConsole(commandsInBlock);
	}

	void inputDynamicSizeBlock(ComandBlockT &commandsInBlock) {
		int endOfDynamicBlockCounter = 1;
		std::string currentComand;
		bool firstLineRecived = false;
		while (endOfDynamicBlockCounter&&inputNewCommand(currentComand))
		{
			if (!firstLineRecived) {
				auto firstCommandTime = std::time(NULL);
				commandsInBlock.emplace_back(std::to_string(firstCommandTime));
				firstLineRecived = true;
			}
			if (currentComand == startDynamicBlock) {
				endOfDynamicBlockCounter++;
			}
			else if (currentComand == endDynamicBlock) {
				endOfDynamicBlockCounter--;
			}
			else {
				commandsInBlock.emplace_back(currentComand);
			}
		}
		if (endOfDynamicBlockCounter == 0) {
			stateQueue.push(state::outputBlock);
			stateQueue.push(state::inputStatcSizeBlock);
		}
	}


	void processCommands(int const& countOfCommandsInBlock) {
		stateQueue.push(state::inputStatcSizeBlock);
		ComandBlockT commandsInBlock;
		while (!stateQueue.empty()) {
			currentState = stateQueue.front();
			stateQueue.pop();
			switch (currentState)
			{
			case state::inputStatcSizeBlock:
				inputSaticSizeBlock(countOfCommandsInBlock, commandsInBlock);
				break;
			case state::inputDynamicSizeBlock:
				inputDynamicSizeBlock(commandsInBlock);
				break;
			case state::outputBlock:
				outputBlock(commandsInBlock);
				commandsInBlock.clear();
				break;
			case state::terminate:
				stateQueue = std::queue<state>{};
				*isInputStreamOver = true;
				break;
			default:
				break;
			}
		}
		for (auto& i : workers) {
			i.join();
		}
		printMetrics();
	}
	void printMetrics() {
		std::cout << "Main thread" << std::endl;
		std::cout << "Count of blocks = " << countOfBlocks
			<< " Count of commands = " << countOfCommands << " Count of lines = " << countOfLines-1<<std::endl;
		size_t threadCounter=0;
		for (auto &i : IOFstreams) {
			size_t countOfBlocks=0, countOfCommands=0,totalSleepTime=0;
			std::tie(countOfBlocks,countOfCommands, totalSleepTime) = i.getCountOfBlocksAndCommands();
			std::cout << "Thread " + std::to_string(threadCounter) << std::endl;
			std::cout << "Count of blocks = " << countOfBlocks << " Count of commands = " << countOfCommands <<" Total sleep time ="<< totalSleepTime <<std::endl;
			threadCounter++;
		}
	}
private:
	size_t countOfBlocks = 0;
	size_t countOfCommands = 0;
	size_t countOfLines = 0;
	std::queue<state> stateQueue;
	std::vector<std::thread> workers;
	std::vector<OFThread> IOFstreams;
	std::shared_ptr<QueueMutexT> queueMutex;
	std::shared_ptr<FileMutexT> fileCreateMutex;
	std::shared_ptr<std::atomic_bool> isInputStreamOver;
	std::shared_ptr<CommandQueueT> commandsQueue;
	state currentState;
	const std::string startDynamicBlock = std::string({ "{" });
	const std::string endDynamicBlock = std::string({ "}" });
};


int main(int argc, char **argv)
{
	if (argc > 1) {
		CommandsProcessor cp{ 2 };
		int countOfCommandsInBlock = atoi(argv[1]);
		cp.processCommands(countOfCommandsInBlock);
	}
	else {
		std::cout << "Please transmit count of commands in block";
		return 0;
	}
	return 0;
}