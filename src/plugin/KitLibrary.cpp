// The kit folder as banks (see KitLibrary.h).
#include "KitLibrary.h"
#include <algorithm>
#include <stdexcept>

namespace md::plugin {

KitLibrary::KitLibrary(juce::File folder) : m_folder(std::move(folder))
{
    m_folder.createDirectory();
    refresh(true);
}

int KitLibrary::indexOf(const juce::String& bankName) const
{
    for (size_t i = 0; i < m_banks.size(); ++i)
        if (m_banks[i].name == bankName) return int(i);
    return -1;
}

juce::Array<juce::File> KitLibrary::syxFiles() const
{
    // the wildcard ignores case where the file system does (macOS)
    auto files = m_folder.findChildFiles(juce::File::findFiles | juce::File::ignoreHiddenFiles, false, "*.syx");
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural(b.getFileName()) < 0; });
    return files;
}

bool KitLibrary::refresh(bool force)
{
    const auto files = syxFiles();
    juce::String signature;
    for (const auto& f : files)
        signature << f.getFileName() << '|' << f.getSize() << '|' << f.getLastModificationTime().toMilliseconds() << '\n';
    if (!force && signature == m_signature) return false;
    m_signature = signature;

    std::vector<Bank> banks;
    m_problems.clear();
    for (const auto& f : files) {
        try {
            auto b = md::library::loadSyxFile(f.getFullPathName().toStdString());
            Bank bank{f, juce::String(b.name), {}};
            for (auto& k : b.kits)
                if (!k.isFree()) bank.kits.push_back(std::move(k));
            if (!bank.kits.empty()) banks.push_back(std::move(bank));   // an OS file or a pattern dump is not a bank
        } catch (const std::exception& e) {
            m_problems.add(f.getFileName() + ": " + e.what());
        }
    }
    m_banks = std::move(banks);
    ++m_revision;
    return true;
}

bool KitLibrary::import(const juce::File& file, juce::String& message, juce::String* bankName)
{
    md::library::Bank bank;
    try {
        bank = md::library::loadSyxFile(file.getFullPathName().toStdString());
    } catch (const std::exception& e) {
        message = file.getFileName() + ": " + e.what();
        return false;
    }
    const int used = bank.usedKits();
    if (used == 0) {
        message = file.getFileName() + " holds no kit";
        return false;
    }
    auto target = m_folder.getChildFile(file.getFileName());
    for (int n = 2; target.exists(); ++n) {
        if (target == file || (target.getSize() == file.getSize() && target.hasIdenticalContentTo(file))) {
            refresh();
            if (bankName != nullptr) *bankName = target.getFileNameWithoutExtension();
            message = target.getFileName() + " is in the kit folder already";
            return true;
        }
        target = m_folder.getChildFile(file.getFileNameWithoutExtension() + " " + juce::String(n) + file.getFileExtension());
    }
    if (!m_folder.createDirectory() || !file.copyFileTo(target)) {
        message = "could not copy " + file.getFileName() + " to " + m_folder.getFullPathName();
        return false;
    }
    refresh(true);
    if (bankName != nullptr) *bankName = target.getFileNameWithoutExtension();
    message = "copied " + target.getFileName() + " (" + juce::String(used) + (used == 1 ? " kit)" : " kits)");
    return true;
}

} // namespace md::plugin
