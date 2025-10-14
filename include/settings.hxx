#pragma once
#include <BetterSMS/settings.hxx>

static const char *sElementsTeam[] = {"MasterMario777",
                                      "Bubbster05",
                                      "StevenShockley",
                                      "theAzack9",
                                      "Sean.O",
                                      "King G",
                                      "Henk Wasmachine",
                                      "MightMang0o",
                                      "UncleMeat",
                                      "NikkeKatski"};

static const char *sLeadStageDev[] = {"MasterMario777"};

static const char *sLeadCoder[] =    {"theAzack9"};

static const char *sLeadComposer[] = {"StevenShockley"};

static const char *sPizzaGuy[] =     {"Sean.O"};

static const char *sModuleDev[] =    {"NikkeKatski/Axolotl"};

static const char *sPlaceholder[] =  {"Finish this screen?"};

class sElemTeamSetting final : public BetterSMS::Settings::IntSetting {
public:
    sElemTeamSetting() : IntSetting("Team Elements", &mIndex) {
        mValueRange = {0, (sizeof(sElementsTeam) / sizeof(const char *)) - 1, 1};
    }
    ~sElemTeamSetting() override {}

    void load(JSUMemoryInputStream &in) override {}
    void save(JSUMemoryOutputStream &out) override {}
    void getValueName(char *dst) const override { strcpy(dst, sElementsTeam[mIndex]); }

private:
    int mIndex;
};

class sLeadStageSetting final : public BetterSMS::Settings::IntSetting {
public:
    sLeadStageSetting() : IntSetting("Lead Stage Dev", &mIndex) {
        mValueRange = {0, (sizeof(sLeadStageDev) / sizeof(const char *)) - 1, 1};
    }
    ~sLeadStageSetting() override {}

    void load(JSUMemoryInputStream &in) override {}
    void save(JSUMemoryOutputStream &out) override {}
    void getValueName(char *dst) const override { strcpy(dst, sLeadStageDev[mIndex]); }

private:
    int mIndex;
};

class sLeadCoderSetting final : public BetterSMS::Settings::IntSetting {
public:
    sLeadCoderSetting() : IntSetting("Lead Coder", &mIndex) {
        mValueRange = {0, (sizeof(sLeadCoder) / sizeof(const char *)) - 1, 1};
    }
    ~sLeadCoderSetting() override {}

    void load(JSUMemoryInputStream &in) override {}
    void save(JSUMemoryOutputStream &out) override {}
    void getValueName(char *dst) const override { strcpy(dst, sLeadCoder[mIndex]); }

private:
    int mIndex;
};

class sLeadComposerSetting final : public BetterSMS::Settings::IntSetting {
public:
    sLeadComposerSetting() : IntSetting("Lead Composer", &mIndex) {
        mValueRange = {0, (sizeof(sLeadComposer) / sizeof(const char *)) - 1, 1};
    }
    ~sLeadComposerSetting() override {}

    void load(JSUMemoryInputStream &in) override {}
    void save(JSUMemoryOutputStream &out) override {}
    void getValueName(char *dst) const override { strcpy(dst, sLeadComposer[mIndex]); }

private:
    int mIndex;
};

class sPizzaGuySetting final : public BetterSMS::Settings::IntSetting {
public:
    sPizzaGuySetting() : IntSetting("Buys Pizza", &mIndex) {
        mValueRange = {0, (sizeof(sPizzaGuy) / sizeof(const char *)) - 1, 1};
    }
    ~sPizzaGuySetting() override {}

    void load(JSUMemoryInputStream &in) override {}
    void save(JSUMemoryOutputStream &out) override {}
    void getValueName(char *dst) const override { strcpy(dst, sPizzaGuy[mIndex]); }

private:
    int mIndex;
};

class sModuleDevSetting final : public BetterSMS::Settings::IntSetting {
public:
    sModuleDevSetting() : IntSetting("Module Code", &mIndex) {
        mValueRange = {0, (sizeof(sModuleDev) / sizeof(const char *)) - 1, 1};
    }
    ~sModuleDevSetting() override {}

    void load(JSUMemoryInputStream &in) override {}
    void save(JSUMemoryOutputStream &out) override {}
    void getValueName(char *dst) const override { strcpy(dst, sModuleDev[mIndex]); }

private:
    int mIndex;
};

class sPlaceholderSetting final : public BetterSMS::Settings::IntSetting {
public:
    sPlaceholderSetting() : IntSetting("Placeholder", &mIndex) {
        mValueRange = {0, (sizeof(sPlaceholder) / sizeof(const char *)) - 1, 1};
    }
    ~sPlaceholderSetting() override {}

    void load(JSUMemoryInputStream &in) override {}
    void save(JSUMemoryOutputStream &out) override {}
    void getValueName(char *dst) const override { strcpy(dst, sPlaceholder[mIndex]); }

private:
    int mIndex;
};
