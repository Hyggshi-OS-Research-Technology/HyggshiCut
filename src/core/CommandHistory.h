#ifndef COMMAND_HISTORY_H
#define COMMAND_HISTORY_H

#include <memory>
#include <vector>
#include <string>
#include <mutex>

namespace Core {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual std::string getName() const = 0;
};

class CommandHistory {
public:
    explicit CommandHistory(size_t maxHistorySize = 100);
    ~CommandHistory() = default;

    void pushAndExecute(std::unique_ptr<ICommand> command);
    bool undo();
    bool redo();

    bool canUndo() const;
    bool canRedo() const;
    void clear();

    std::string getUndoName() const;
    std::string getRedoName() const;

private:
    std::vector<std::unique_ptr<ICommand>> m_history;
    size_t m_currentIndex{0};
    size_t m_maxHistorySize{100};
    mutable std::mutex m_mutex;
};

} // namespace Core

#endif // COMMAND_HISTORY_H