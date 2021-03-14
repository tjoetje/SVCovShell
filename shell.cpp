/**
	* Shell
	* Operating Systems
	* v20.08.28
	*/

/**
	Hint: Control-click on a functionname to go to the definition
	Hint: Ctrl-space to auto complete functions and variables
	*/

// function/class definitions you are going to use
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/param.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <vector>

// although it is good habit, you don't have to type 'std' before many objects by including this line
using namespace std;

struct Command {
	vector<string> parts = {};
};

struct Expression {
	vector<Command> commands;
	string inputFromFile;
	string outputToFile;
	bool background = false;
};

// Parses a string to form a vector of arguments. The seperator is a space char (' ').
vector<string> splitString(const string& str, char delimiter = ' ') {
	vector<string> retval;
	for (size_t pos = 0; pos < str.length(); ) {
		// look for the next space
		size_t found = str.find(delimiter, pos);
		// if no space was found, this is the last word
		if (found == string::npos) {
			retval.push_back(str.substr(pos));
			break;
		}
		// filter out consequetive spaces
		if (found != pos)
			retval.push_back(str.substr(pos, found-pos));
		pos = found+1;
	}
	return retval;
}

// wrapper around the C execvp so it can be called with C++ strings (easier to work with)
// always start with the command itself
// always terminate with a NULL pointer
// DO NOT CHANGE THIS FUNCTION UNDER ANY CIRCUMSTANCE
int execvp(const vector<string>& args) {
	// build argument list
	const char** c_args = new const char*[args.size()+1];
	for (size_t i = 0; i < args.size(); ++i) {
		c_args[i] = args[i].c_str();
	}
	c_args[args.size()] = nullptr;
	// replace current process with new process as specified
	::execvp(c_args[0], const_cast<char**>(c_args));
	// if we got this far, there must be an error
	int retval = errno;
	// in case of failure, clean up memory
	delete[] c_args;
	return retval;
}

// Executes a command with arguments. In case of failure, returns error code.
int executeCommand(const Command& cmd) {
	auto& parts = cmd.parts;
	if (parts.size() == 0)
		return EINVAL;

	// execute external commands
	int retval = execvp(parts);
	return retval;
}

void displayPrompt() {
	char buffer[512];
	char* dir = getcwd(buffer, sizeof(buffer));
	if (dir) {
		cout << "\e[32m" << dir << "\e[39m"; // the strings starting with '\e' are escape codes, that the terminal application interpets in this case as "set color to green"/"set color to default"
	}
	cout << "$ ";
	flush(cout);
}

string requestCommandLine(bool showPrompt) {
	if (showPrompt) {
		displayPrompt();
	}
	string retval;
	getline(cin, retval);
	return retval;
}

// note: For such a simple shell, there is little need for a full blown parser (as in an LL or LR capable parser).
// Here, the user input can be parsed using the following approach.
// First, divide the input into the distinct commands (as they can be chained, separated by `|`).
// Next, these commands are parsed separately. The first command is checked for the `<` operator, and the last command for the `>` operator.
Expression parseCommandLine(string commandLine) {
	Expression expression;
	vector<string> commands = splitString(commandLine, '|');
	for (size_t i = 0; i < commands.size(); ++i) {
		string& line = commands[i];
		vector<string> args = splitString(line, ' ');
		if (i == commands.size() - 1 && args.size() > 1 && args[args.size()-1] == "&") {
			expression.background = true;
			args.resize(args.size()-1);
		}
		if (i == commands.size() - 1 && args.size() > 2 && args[args.size()-2] == ">") {
			expression.outputToFile = args[args.size()-1];
			args.resize(args.size()-2);
		}
		if (i == 0 && args.size() > 2 && args[args.size()-2] == "<") {
			expression.inputFromFile = args[args.size()-1];
			args.resize(args.size()-2);
		}
		expression.commands.push_back({args});
	}
	return expression;
}

bool checkCommandExists(Command command){
	return !executeCommand(command);
}

int execMultiExtCmd(Expression& expression){
	// Make a vector of children in order to keep track of children processes. -suggestion from Hans Lous
	vector<pid_t> no_child;
	// We first have to make the pipes before forking. -suggestion from Hans Lous
	const int NO_PIPES = expression.commands.size() - 1;
	int pipeFDs[NO_PIPES][2]; //0 is read, 1 is write
	for (int i = 0; i < NO_PIPES; i++){
    	if (pipe(pipeFDs[i]) < 0){
			cerr << "Pipe could not be opened" << endl;
    		exit(1);
		}
	}	

	pid_t child1 = fork();
	if (child1 < 0){
		cerr << "Fork Failed" << endl;
		exit(1);
	}
	// Check if there is an input file (first command ends with <)
	if (child1 == 0){
		if (expression.inputFromFile.size() != 0){
			int inputFile = open(expression.inputFromFile.c_str(), O_RDONLY);
			if (inputFile < 0){
				cerr << "Input file cannot be found or does not exist" << endl;
				abort();
			}
			// redirect the output of the inputfile to the standard input (STDIN_FILENO).
			dup2(inputFile, STDIN_FILENO);
			close(inputFile); // free non used resources
		}
	}
	//store the child in the vector so we can easily identify and wait for it later
	no_child.push_back(child1);
	for (int i = 0; i < expression.commands.size() - 1 ; i++){
		if (no_child[i] == 0) {
			close(pipeFDs[i][0]);
			//redirect STDOUT_FILENO to input of pipe
			if (dup2(pipeFDs[i][1], STDOUT_FILENO) < 0){
				cerr << "child could not dup" << endl;
       			exit(1);
			} 
			close(pipeFDs[i][1]); //free non used resources
			// check if command[i] exists
			if (checkCommandExists(expression.commands[i])){
				if (executeCommand(expression.commands[i]) < 0){
					cerr << "Execute command failed" << endl;
					abort();
				}
			}
			else {
					cout << "Command not found:" + expression.commands[i].parts[0] << endl;
					abort();
				}
		}
		pid_t child2 = fork();

		if (child2 < 0){
				cerr << "Fork Failed" << endl;
				exit(1);
		}
		// Check if it is the last command, if it is then execute it 
		// otherwise give it to the next pipe
		if (i == expression.commands.size()-2){
			if (child2 == 0) {
				// Check if there is an outputfile (last command ends with >)
				if (expression.outputToFile.size() != 0){
					int outputFile = open(expression.outputToFile.c_str(), O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
					if (outputFile < 0){
						cerr << "Output file cannot be created or does not exist" << endl;
						abort();
					}
					// redirect standard output (STDOUT_FILENO) to the input of the outputfile
					if (dup2(outputFile, STDOUT_FILENO) < 0){
						cerr << "child2 could not dup" << endl;
       					exit(1);
					}
					close(outputFile); //free non used resources
				} 
				//redirect output of the last pipe to STDIN_FILENO
				close(pipeFDs[i][1]);
				if (dup2(pipeFDs[i][0], STDIN_FILENO) < 0){
					cerr << "child2 could not dup" << endl;
       				exit(1);
				}		
				close(pipeFDs[i][0]); //free non used resources
				//check if last command is valid
				if (checkCommandExists(expression.commands[i+1])) {
					if (executeCommand(expression.commands[i+1]) < 0){
						cerr << "Execute command failed" << endl;
						abort();
					}
				}
				else {
					cout << "Command not found:" + expression.commands[i+1].parts[0] << endl;
					abort();
				}
			}
		}
		// connect to the next pipe
		if (child2 == 0){
			if (dup2(pipeFDs[i][0], STDIN_FILENO) < 0){
				cerr << "child2 could not dup" << endl;
       			exit(1);
			}
		}
		//close FDs of parent
		close(pipeFDs[i][0]); 
		close(pipeFDs[i][1]);
		//store child2 in vector
		no_child.push_back(child2);
	}
	// do not wait for children
	if (expression.background){
		cout << "Running in background" << endl;
		return 0;
	}
	//wait for all the child processes
	for (int i = 0; i < no_child.size(); i++){
		waitpid(no_child[i], nullptr, 0);
	}
	return 0;
}

int execSingleExtCmd(Expression& expression){
	pid_t child1 = fork();
	if (child1 < 0){
		cerr << "Fork Failed" << endl;
		exit(1);
	}
	if (child1 == 0) {
		// if command ends with < redirect to inputfile
		if (expression.inputFromFile.size() != 0){
			int inputFile = open(expression.inputFromFile.c_str(), O_RDONLY);
			if (inputFile < 0){
				cerr << "Input file cannot be found or does not exist" << endl;
				abort();
			}
			// redirect the output of the inputfile to the standard input (STDIN_FILENO).
			dup2(inputFile, STDIN_FILENO);
			close(inputFile); // free non used resources
		}
		// if command ends with > redirect to outputfile
		if (expression.outputToFile.size() != 0){
			int outputFile = open(expression.outputToFile.c_str(), O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
			if (outputFile < 0){
				cerr << "Output file cannot be created or does not exist" << endl;
				abort();
			}
			// redirect standard output (STDOUT_FILENO) to the input of the outputfile
			dup2(outputFile, STDOUT_FILENO);
			close(outputFile); // free non used resources
		}
		// do not execute non-existing commands
		if (checkCommandExists(expression.commands[0])){
			if(executeCommand(expression.commands[0]) < 0){
				cerr << "Execute command failed" << endl;
				abort();
			}
		}
		else {
			cout << "Command not found:" + expression.commands[0].parts[0] << endl;
			abort();
		}
	}
	if (expression.background){
		cout << "Running in background" << endl;
		return 0;
	}
	waitpid(child1, nullptr, 0);
	return 0;
}

int executeExpression(Expression& expression) {
	// Check for empty expression
	if (expression.commands.size() == 0)
		return EINVAL;

	// Handle intern commands (like 'cd' and 'exit')
	// For some reason I cannot only return 1 since I
	// get "Operation not permitted" while it has full access 
	if (expression.commands[0].parts[0] == "exit"){
		cout << "Exiting the shell" << endl;
		exit(1);
		return 1;
	}
	if (expression.commands[0].parts[0] == "cd"){
		chdir(expression.commands[0].parts[1].c_str());
		return 0;
	}
	// Handle multiple external commands with or without arguments
	if (expression.commands.size() > 1){
		return execMultiExtCmd(expression);
	}
		
	// Handle single external commands with or without arguments
	return execSingleExtCmd(expression);
}

int normal(bool showPrompt) {
	while (cin.good()) {
		string commandLine = requestCommandLine(showPrompt);
		Expression expression = parseCommandLine(commandLine);
		int rc = executeExpression(expression);
		if (rc != 0)
			cerr << strerror(rc) << endl;
	}
	return 0;
}

// framework for executing "date | tail -c 5" using raw commands
// two processes are created, and connected to each other
int step1(bool showPrompt) {
	// create communication channel shared between the two processes
	// ...
	int pipeFDs[2]; //0 is read, 1 is write
	
	if (pipe(pipeFDs) < 0){
		cerr << "Pipe could not be opened!" << endl;
        exit(1);
    }

	pid_t child1 = fork();
	if (child1 < 0){
		cerr << "Fork Failed" << endl;
		exit(1);
	}
	if (child1 == 0) {
		// redirect standard output (STDOUT_FILENO) to the input of the shared communication channel
		close(pipeFDs[0]);
		if (dup2(pipeFDs[1], STDOUT_FILENO) < 0){
			cerr << "child1 could not dup" << endl;
        	exit(1);
		} 
		// free non used resources (why?) 
		// we close pipes (two FDs, read end and write end) in order not to have 
		// too many FDs open (prevent from reaching a limit of open FDs).
		// also by closing the pipe it becomes more comprehensible which process is
		// doing the writing or reading on the pipe.
		close(pipeFDs[1]);
		Command cmd = {{string("date")}};
		if(executeCommand(cmd) < 0){
			cerr << "Execute command failed" << endl;
			exit(1);
		}
		// display nice warning that the executable could not be found
		abort(); // if the executable is not found, we should abort. (why?)
		// because the child process should stop.
	}

	pid_t child2 = fork();
	if (child2 < 0){
		cerr << "Fork Failed" << endl;
		exit(1);
	}
	if (child2 == 0) {
		// redirect the output of the shared communication channel to the standard input (STDIN_FILENO).
		close(pipeFDs[1]);
		if (dup2(pipeFDs[0], STDIN_FILENO) < 0){
			cerr << "child2 could not dup" << endl;
        	exit(1);
		} 
		// free non used resources (why?)
		// same answer as above
		close(pipeFDs[0]);
		Command cmd = {{string("tail"), string("-c"), string("5")}};
		if (executeCommand(cmd) < 0){
			cerr << "Execute command failed" << endl;
			exit(1);
		}
		abort(); // if the executable is not found, the child process should stop (to avoid having two shells active)
	}
	// free non used resources (why?)
	// the parent still has both FDs open because of pipe(), we close them since we do not use them.
	close(pipeFDs[0]);
	close(pipeFDs[1]);
	// wait on child processes to finish (why both?)
	// we wait on both child processes in order to avoid both of them continuing after its parent exits.
	waitpid(child1, nullptr, 0);
	waitpid(child2, nullptr, 0);
	return 0;
}

int shell(bool showPrompt) {
	//*
	return normal(showPrompt);
	/*/
	return step1(showPrompt);
	//*/
}
