#include <ncursesw/ncurses.h>
#include <dirent.h>
#include <string>
#include <vector>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <fstream> // For reading files
#include <stack> // For backward/forward navigation

#ifdef _WIN32
#include <windows.h>
#include <tchar.h>
#include <direct.h>
#include <sys/stat.h>
#define getcwd _getcwd
#define stat _stat // Use _stat for Windows
#else

#include <sys/stat.h>
#include <pwd.h>
#include <signal.h>
#include <grp.h>
#include <unistd.h>
#include <sys/wait.h>  // For handling child processes
#include <time.h>
#endif

// Function to format permissions (Unix-like systems only)
#ifndef _WIN32
std::string formatPermissions(mode_t mode) {
    std::string perms = (S_ISDIR(mode)) ? "d" : "-";
    perms += (mode & S_IRUSR) ? "r" : "-";
    perms += (mode & S_IWUSR) ? "w" : "-";
    perms += (mode & S_IXUSR) ? "x" : "-";
    perms += (mode & S_IRGRP) ? "r" : "-";
    perms += (mode & S_IWGRP) ? "w" : "-";
    perms += (mode & S_IXGRP) ? "x" : "-";
    perms += (mode & S_IROTH) ? "r" : "-";
    perms += (mode & S_IWOTH) ? "w" : "-";
    perms += (mode & S_IXOTH) ? "x" : "-";
    return perms;
}
#endif

void handle_resize(int sig){
    clear();
    refresh();
    getmaxyx(stdscr, LINES, COLS);
    mvprintw(0,0, "New Size: %dx%d", COLS, LINES);
}

// Function to get directory contents
std::vector<std::string> getDirectoryContents(const std::string &dirPath) {
    std::vector<std::string> contents;
    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir(dirPath.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            std::string fileName = ent->d_name;
            std::string fullPath = dirPath + "/" + fileName;

#ifdef _WIN32
            // For Windows, retrieve file attributes using the Windows API
            WIN32_FILE_ATTRIBUTE_DATA fileInfo;
            if (GetFileAttributesEx(fullPath.c_str(), GetFileExInfoStandard, &fileInfo)) {
                SYSTEMTIME st;
                FileTimeToSystemTime(&fileInfo.ftLastWriteTime, &st);

                std::stringstream entry;
                entry << ((fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "d" : "-")
                      << "    " << std::setfill('0')<< std::setw(2) << st.wDay << "/" << std::setw(2) << st.wMonth << "/"<< std::setw(4) << st.wYear
                      << "    " << std::setw(2) << st.wHour << ":" << std::setw(2) << st.wMinute << "    " << fileName;

                contents.push_back(entry.str());
            }
#else
            // Unix-like systems: use stat to get file metadata
            struct stat fileStat;
            if (stat(fullPath.c_str(), &fileStat) == 0) {
                std::stringstream entry;
                entry << formatPermissions(fileStat.st_mode) << " ";
                entry << fileStat.st_size << " "; // File size
                char timeBuf[80];
                struct tm *timeInfo = localtime(&fileStat.st_mtime);
                strftime(timeBuf, sizeof(timeBuf), "%b %d %H:%M", timeInfo);
                entry << timeBuf << " ";
                entry << fileName;

                contents.push_back(entry.str());
            }
#endif
        }
        closedir(dir);
    }

    return contents;
}

// Function to check if the file is readable
bool isReadable(const std::string &filePath) {
    std::ifstream file(filePath);
    return file.good();
}

// Function to open the file with the default application if it is not readable
void openWithDefaultApplication(const std::string &filePath) {
#ifdef _WIN32
    // Use ShellExecute to open the file in the default application on Windows
    ShellExecute(NULL, "open", filePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
    // Unix-like systems: Use fork and exec to open the file with default application
    pid_t pid = fork();
    if (pid == 0) {
        // Child process: Try to open the file using `xdg-open` or `open` for macOS
        execlp("xdg-open", "xdg-open", filePath.c_str(), (char *)NULL);  // For Linux
        execlp("open", "open", filePath.c_str(), (char *)NULL);          // For macOS
        exit(1);  // If exec fails, terminate child process
    } else if (pid > 0) {
        // Parent process: Wait for child to complete
        int status;
        waitpid(pid, &status, 0);
    }
#endif
}

// Modified function to display file content with scrollable functionality
void displayFileContent(const std::string &filePath) {
    // First, check if the file is readable
    if (!isReadable(filePath)) {
        mvprintw(0, 0, "File not readable. Opening with default application...");
        refresh();
        openWithDefaultApplication(filePath);  // Open with the default application
        return;  // Exit this function after launching the external application
    }

    // Continue with the previous logic if the file is readable
    std::ifstream file(filePath);
    if (!file) {
        mvprintw(0, 0, "Error: Unable to open file");
        return;
    }

    // Read all file lines into a vector for easy access
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }

    int totalLines = lines.size();
    int visibleRows = LINES - 2;  // Subtract 2 for file name and status line

    int topLine = 0;  // The first visible line
    int currentLine = 0;  // The line the cursor is currently on

    int ch;
    bool quit = false;

    while (!quit) {
        erase();

        // Display file name at the top
        mvprintw(0, 0, "File: %s", filePath.c_str());

        // Display the visible portion of the file
        for (int i = 0; i < visibleRows && (topLine + i) < totalLines; ++i) {
            if (topLine + i == currentLine) {
                attron(A_REVERSE);  // Highlight the current line
            }
            mvprintw(i + 1, 0, "%s", lines[topLine + i].c_str());
            if (topLine + i == currentLine) {
                attroff(A_REVERSE);
            }
        }

        // Display navigation instructions at the bottom
        mvprintw(LINES - 1, 0, "Use arrow keys to navigate, ESC to exit");

        // Handle user input
        ch = getch();
        switch (ch) {
        case KEY_UP:
            if (currentLine > 0) {
                currentLine--;
            }
            if (currentLine < topLine) {
                topLine--;
            }
            break;
        case KEY_DOWN:
            if (currentLine < totalLines - 1) {
                currentLine++;
            }
            if (currentLine >= topLine + visibleRows) {
                topLine++;
            }
            break;
        case 27:  // ESC key to exit
            quit = true;
            break;
        }
    }

    // Wait for user input to return to the directory view
    erase();
    mvprintw(0, 0, "Returning to directory view...");
    refresh();
    getch();  // Wait for user input before exiting
}

int main() {
    // Initialize ncurses
    initscr();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    // signal(SIG, handle_resize);

    // Track navigation history for back/forward functionality
    std::stack<std::string> backStack;
    std::stack<std::string> forwardStack;

    // Get current working directory
    char cwd[256];
    getcwd(cwd, sizeof(cwd));
    std::string currentDir(cwd);

    // Get directory contents
    std::vector<std::string> contents = getDirectoryContents(currentDir);

    // Main loop
    int choice = 0;
    while (true) {
        // Clear screen
        erase();

        /*if(is_term_resized()){
            clear();
        }*/

        // Print directory path
        mvprintw(0, 0, "Directory: %s", currentDir.c_str());

        // Print directory contents
        for (int i = 0; i < contents.size(); i++) {
            if (i == choice) {
                attron(A_REVERSE);
            }
            mvprintw(i + 1, 0, "%s", contents[i].c_str());
            if (i == choice) {
                attroff(A_REVERSE);
            }
        }

        // Handle user input
        int ch = getch();
        switch (ch) {
        case KEY_UP:
            choice = std::max(0, choice - 1);
            break;
        case KEY_DOWN:
            choice = std::min((int)contents.size() - 1, choice + 1);
            break;
        case 10: {
            // Enter key to enter a directory or view file content
            std::string selected = contents[choice].substr(contents[choice].find_last_of(" ") + 1);
            std::string selectedPath = currentDir + "/" + selected;

            struct stat fileStat;
            if (stat(selectedPath.c_str(), &fileStat) == 0) {
                if (S_ISDIR(fileStat.st_mode)) {
                    // Enter selected directory
                    if (selected == "..") {
                        // Go to parent directory
                        size_t lastSlash = currentDir.find_last_of('/');
                        if (lastSlash != std::string::npos) {
                            forwardStack = std::stack<std::string>();  // Clear forward stack
                            backStack.push(currentDir); // Push current directory to back stack
                            currentDir = currentDir.substr(0, lastSlash);
                        }
                    } else {
                        forwardStack = std::stack<std::string>();  // Clear forward stack
                        backStack.push(currentDir); // Push current directory to back stack
                        currentDir = selectedPath;
                    }
                    contents = getDirectoryContents(currentDir);
                    choice = 0;
                } else if (S_ISREG(fileStat.st_mode)) {
                    // If it's a regular file, display its content
                    displayFileContent(selectedPath);
                }
            }
            break;
        }
        case KEY_LEFT:
            // Navigate back in history
            if (!backStack.empty()) {
                forwardStack.push(currentDir); // Push current directory to forward stack
                currentDir = backStack.top();
                backStack.pop();
                contents = getDirectoryContents(currentDir);
                choice = 0;
            }
            break;
        case KEY_RIGHT:
            // Navigate forward in history
            if (!forwardStack.empty()) {
                backStack.push(currentDir); // Push current directory to back stack
                currentDir = forwardStack.top();
                forwardStack.pop();
                contents = getDirectoryContents(currentDir);
                choice = 0;
            }
            break;
        case 27: // Escape key
            goto exit;
        }
    }

exit:
    // Clean up
    endwin();
    return 0;
}
