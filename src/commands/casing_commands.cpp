#include "commands/casing_commands.h"

#include "akashi/area_rule.h"
#include "akashi/filesystem_service.h"
#include "akashi/helpers/parse.h"
#include "akashi/permissions.h"
#include "core/client_session.h"
#include "core/command_context.h"
#include "core/command_registry.h"
#include "core/command_spec.h"
#include "core/server_context.h"
#include "core/server_settings.h"
#include "proto/packet.h"
#include "world/area.h"

#include <limits>

namespace akashi::commands {

// Command handlers dispatch area events through the invoking client.
static void runCasingAfterRule(CommandContext &f_context, const QString &f_event, const QVariantMap &f_payload)
{
    akashi::ClientSession *l_client = f_context.self();
    if (l_client) {
        l_client->runAfterRule(f_event, f_payload);
    }
}

// A blocking before-rule returns its reason; a missing client never blocks.
static std::optional<QString> checkCasingBeforeRule(CommandContext &f_context, const QString &f_event, const QVariantMap &f_payload)
{
    akashi::ClientSession *l_client = f_context.self();
    if (l_client) {
        return l_client->checkBeforeRule(f_event, f_payload);
    }
    return std::nullopt;
}

void cmdDoc(CommandContext &f_context, const Args &f_args)
{
    if (!f_args.has(QStringLiteral("text"))) {
        f_context.reply("Document: " + f_context.server()->areaById(f_context.areaId())->document());
        return;
    }
    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }
    if (auto l_refusal = l_client->changeDocument(f_args.text(QStringLiteral("text")))) {
        f_context.reply(*l_refusal);
    }
}

void cmdClearDoc(CommandContext &f_context, const Args &)
{
    akashi::ClientSession *l_client = f_context.self();
    if (!l_client) {
        return;
    }
    if (auto l_refusal = l_client->changeDocument(QString())) {
        f_context.reply(*l_refusal);
    }
}

void cmdEvidenceMod(CommandContext &f_context, const Args &f_args)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    // The word arrives in its declared spelling; hiddencm is the old name.
    const QString l_mod = f_args.text(QStringLiteral("mod"));
    akashi::EvidenceStore::Access l_access = akashi::EvidenceStore::Access::FreeForAll;
    QString l_access_name = QStringLiteral("ffa");
    if (l_mod == QLatin1String("cm")) {
        l_access = akashi::EvidenceStore::Access::Cm;
        l_access_name = QStringLiteral("cm");
    }
    else if (l_mod == QLatin1String("mod")) {
        l_access = akashi::EvidenceStore::Access::Mod;
        l_access_name = QStringLiteral("mod");
    }
    else if (l_mod == QLatin1String("hidden_cm") || l_mod == QLatin1String("hiddencm")) {
        l_access = akashi::EvidenceStore::Access::HiddenCm;
        l_access_name = QStringLiteral("hidden_cm");
    }

    // Changing who may touch the evidence is an edit of the record.
    const QVariantMap l_payload = {{QStringLiteral("access"), l_access_name}};
    if (auto l_rule_block = checkCasingBeforeRule(f_context, akashi::AreaEvents::EvidenceEdited, l_payload)) {
        f_context.reply(*l_rule_block);
        return;
    }
    l_area->setEvidenceAccess(l_access);
    f_context.reply("Changed evidence mod.");

    akashi::ClientSession *l_self = f_context.self();
    l_self->sendEvidenceList(l_area);
    runCasingAfterRule(f_context, akashi::AreaEvents::EvidenceEdited, l_payload);
}

void cmdEvidenceSwap(CommandContext &f_context, const Args &f_args)
{
    akashi::Area *l_area = f_context.server()->areaById(f_context.areaId());
    int l_ev_size = l_area->evidence().size() - 1;

    if (l_ev_size < 0) {
        f_context.reply("No evidence in area.");
        return;
    }

    // The ids are declared as numbers; how many pieces exist is only known here.
    const int l_ev_id1 = int(f_args.number(QStringLiteral("evidence_id")));
    const int l_ev_id2 = int(f_args.number(QStringLiteral("other_evidence_id")));
    for (const int l_id : {l_ev_id1, l_ev_id2}) {
        if (l_id > l_ev_size) {
            f_context.reply(helpers::ParseError(helpers::ParseError::OutOfRange, QString::number(l_id), QStringLiteral("evidence_id"), 0, l_ev_size).errorString());
            return;
        }
    }

    const QVariantMap l_payload = {{QStringLiteral("indexes"), QVariantList{l_ev_id1, l_ev_id2}}};
    if (auto l_rule_block = checkCasingBeforeRule(f_context, akashi::AreaEvents::EvidenceEdited, l_payload)) {
        f_context.reply(*l_rule_block);
        return;
    }
    l_area->swapEvidence(l_ev_id1, l_ev_id2);
    f_context.self()->sendEvidenceList(l_area);
    f_context.reply("The evidence " + QString::number(l_ev_id1) + " and " + QString::number(l_ev_id2) + " have been swapped.");
    runCasingAfterRule(f_context, akashi::AreaEvents::EvidenceEdited, l_payload);
}

void cmdTestify(CommandContext &f_context, const Args &)
{
    akashi::TestimonyRecorder *l_recorder = f_context.server()->areaById(f_context.areaId())->testimonyRecorder();
    if (l_recorder->state() == akashi::TestimonyRecorder::State::Recording) {
        f_context.reply("Testimony recording is already in progress. Please stop it with /pause before starting a new one.");
    }
    else {
        l_recorder->clear();
        l_recorder->setState(akashi::TestimonyRecorder::State::Recording);
        f_context.reply("Started testimony recording. The next IC message will be a title. Use /pause to stop recording.");
    }
}

void cmdExamine(CommandContext &f_context, const Args &)
{
    akashi::TestimonyRecorder *l_recorder = f_context.server()->areaById(f_context.areaId())->testimonyRecorder();
    if (l_recorder->statementCount() - 1 > 0) {
        if (l_recorder->state() == akashi::TestimonyRecorder::State::Playback) {
            f_context.reply("An examination is already running. Use /testimony to view the testimony.");
        }
        else {
            l_recorder->restart();
            f_context.server()->broadcast(akashi::Packet("RT", {"testimony2", "0"}), f_context.areaId());
            // The title replays through the server's IC broadcast.
            f_context.server()->broadcastIc(l_recorder->statementAt(0)->playbackFields(), f_context.areaId());
            return;
        }
    }
    else {
        f_context.reply("Unable to start replay without prior testimony. Use /testify to start or load a testimony with the command: /loadtestimony.");
    }
}

void cmdTestimony(CommandContext &f_context, const Args &)
{
    akashi::TestimonyRecorder *l_recorder = f_context.server()->areaById(f_context.areaId())->testimonyRecorder();
    if (l_recorder->statementCount() - 1 < 1) {
        f_context.reply("Unable to display empty testimony.");
        return;
    }

    QString l_ooc_message;
    for (int i = 1; i < l_recorder->statementCount(); i++) {
        l_ooc_message.append("[" + QString::number(i) + "]" + l_recorder->statementAt(i)->message() + "\n");
    }
    f_context.reply(l_ooc_message);
}

void cmdDelete(CommandContext &f_context, const Args &)
{
    akashi::TestimonyRecorder *l_recorder = f_context.server()->areaById(f_context.areaId())->testimonyRecorder();
    const int l_c_statement = l_recorder->statementIndex();
    if (l_recorder->statementCount() - 1 == 0) {
        f_context.reply("Unable to delete statement. No statements saved in this area.");
    }
    if (l_c_statement > 0 && l_recorder->statementCount() > 2) {
        l_recorder->remove(l_c_statement);
        f_context.reply("The statement with id " + QString::number(l_c_statement) + " has been deleted from the testimony.");
    }
}

void cmdUpdate(CommandContext &f_context, const Args &)
{
    f_context.server()->areaById(f_context.areaId())->testimonyRecorder()->setState(akashi::TestimonyRecorder::State::Update);
    f_context.reply("The next IC-Message will replace the currently selected testimony line.");
}

void cmdPause(CommandContext &f_context, const Args &)
{
    f_context.server()->areaById(f_context.areaId())->testimonyRecorder()->setState(akashi::TestimonyRecorder::State::Stopped);
    f_context.server()->broadcast(akashi::Packet("RT", {"testimony1", "1"}), f_context.areaId());
    f_context.reply("Testimony has been stopped. Use /examine to begin cross-examination.");
}

void cmdAdd(CommandContext &f_context, const Args &)
{
    akashi::TestimonyRecorder *l_recorder = f_context.server()->areaById(f_context.areaId())->testimonyRecorder();
    if (l_recorder->statementIndex() < f_context.server()->serverSettings()->maximum_statements()) {
        l_recorder->setState(akashi::TestimonyRecorder::State::Add);
        f_context.reply("The next IC-Message will be inserted into the testimony.");
    }
    else {
        f_context.reply("Unable to add anymore statements. Please remove any unused ones.");
    }
}

void cmdSaveTestimony(CommandContext &f_context, const Args &f_args)
{
    // The gate is the spec's save_testimony requirement; /permitsaving
    // hands that permission to a person as a grant, so no session flag
    // and no inline check exist anymore.
    akashi::TestimonyRecorder *l_recorder = f_context.server()->areaById(f_context.areaId())->testimonyRecorder();
    if (l_recorder->statementCount() - 1 <= 0) {
        f_context.reply("Can't save an empty testimony.");
        return;
    }

    const std::optional<QString> l_testimony_name = akashi::FileSystemService::sanitizedFileName(f_args.text(QStringLiteral("name")));
    const std::optional<QString> l_path =
        l_testimony_name ? f_context.server()->fileSystem()->resolve(akashi::FileSystemService::Scope::Storage, "testimony/" + *l_testimony_name + ".txt") : std::nullopt;
    if (!l_path) {
        f_context.reply("Invalid testimony name. Use only letters, numbers, dashes and underscores.");
        return;
    }
    if (QFile::exists(*l_path)) {
        f_context.reply("Unable to save testimony. Testimony name already exists.");
        return;
    }

    QByteArray l_data;
    for (int i = 0; i < l_recorder->statementCount(); i++) {
        l_data += l_recorder->statementAt(i)->toSavedLine().toUtf8() + "\n";
    }
    if (auto l_error = f_context.server()->fileSystem()->writeFile(*l_path, l_data)) {
        f_context.reply("Unable to save testimony: " + *l_error);
        return;
    }
    f_context.reply("Testimony saved. To load it use /loadtestimony " + *l_testimony_name);
}

void cmdLoadTestimony(CommandContext &f_context, const Args &f_args)
{
    QDir l_dir_testimony("storage/testimony");
    if (!l_dir_testimony.exists()) {
        f_context.reply("Unable to load testimonies. Testimony storage not found.");
        return;
    }

    const std::optional<QString> l_testimony_name = akashi::FileSystemService::sanitizedFileName(f_args.text(QStringLiteral("name")));
    const std::optional<QString> l_path =
        l_testimony_name ? f_context.server()->fileSystem()->resolve(akashi::FileSystemService::Scope::Storage, "testimony/" + *l_testimony_name + ".txt") : std::nullopt;
    if (!l_path) {
        f_context.reply("Invalid testimony name. Use only letters, numbers, dashes and underscores.");
        return;
    }
    QFile l_file(*l_path);
    if (!l_file.exists()) {
        f_context.reply("Unable to load testimony. Testimony name not found.");
        return;
    }
    if (!l_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        f_context.reply("Unable to load testimony. Permission denied.");
        return;
    }

    akashi::TestimonyRecorder *l_recorder = f_context.server()->areaById(f_context.areaId())->testimonyRecorder();
    l_recorder->clear();
    int l_line_number = 0;
    QTextStream l_in(&l_file);
    while (!l_in.atEnd()) {
        const QString l_line = l_in.readLine();
        ++l_line_number;
        if (l_line.isEmpty()) {
            continue;
        }
        if (l_recorder->statementCount() > f_context.server()->serverSettings()->maximum_statements()) {
            f_context.reply("Testimony too large to be loaded.");
            l_recorder->clear();
            return;
        }
        const std::optional<akashi::Statement> l_statement = akashi::Statement::fromSavedLine(l_line);
        if (!l_statement) {
            f_context.reply("Unable to load testimony. Line " + QString::number(l_line_number) + " is not a statement.");
            l_recorder->clear();
            return;
        }
        l_recorder->insert(l_recorder->statementCount(), *l_statement);
    }
    f_context.reply("Testimony loaded successfully. Use /examine to start playback.");
}

void registerCasingCommands(CommandRegistry &f_registry)
{
    using namespace akashi::permission;
    const QString l_core = QStringLiteral("core");
    const Param l_name = param::token(QStringLiteral("name"));

    {
        CommandSpec l_doc;
        l_doc.name = QStringLiteral("doc");
        l_doc.description = QStringLiteral("Views or sets the document for the area.");
        l_doc.variants = {
            {.id = QStringLiteral("view"), .gate = {casing_doc}, .description = QStringLiteral("Shows the document."), .params = {}, .run = cmdDoc},
            {.id = QStringLiteral("set"), .gate = {casing_doc}, .description = QStringLiteral("Sets the document."), .params = {param::text(QStringLiteral("text"))}, .run = cmdDoc},
        };
        f_registry.registerCommand(l_doc, l_core);
    }
    f_registry.registerCommand(singleForm(QStringLiteral("cleardoc"), {casing_cleardoc}, {}, QStringLiteral("Clears the document in the area."), cmdClearDoc), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("evidence_mod"), {modify_evidence},
                                          {param::word(QStringLiteral("mod"), {QStringLiteral("cm"), QStringLiteral("mod"), QStringLiteral("hidden_cm"), QStringLiteral("hiddencm"), QStringLiteral("ffa")})},
                                          QStringLiteral("Changes the evidence mod in the area."), cmdEvidenceMod),
                               l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("evidence_swap"), {cm_evidence_swap},
                                          {param::integer(QStringLiteral("evidence_id"), 0, std::numeric_limits<int>::max()),
                                           param::integer(QStringLiteral("other_evidence_id"), 0, std::numeric_limits<int>::max())},
                                          QStringLiteral("Swaps two pieces of evidence in the area."), cmdEvidenceSwap),
                               l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("testify"), {cm_testify}, {}, QStringLiteral("Starts testimony recording."), cmdTestify), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("examine"), {cm_examine}, {}, QStringLiteral("Starts testimony playback."), cmdExamine), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("testimony"), {casing_testimony}, {}, QStringLiteral("Lists the statements in the current testimony."), cmdTestimony), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("delete"), {cm_delete}, {}, QStringLiteral("Deletes the currently selected testimony statement."), cmdDelete), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("update"), {cm_update}, {}, QStringLiteral("Replaces the current testimony statement with the next IC message."), cmdUpdate), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("pause"), {cm_pause}, {}, QStringLiteral("Pauses testimony recording or playback."), cmdPause, {QStringLiteral("end")}), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("add"), {cm_add}, {}, QStringLiteral("Inserts a new statement after the current one."), cmdAdd), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("savetestimony"), {save_testimony}, {l_name}, QStringLiteral("Saves the current testimony to a file."), cmdSaveTestimony), l_core);
    f_registry.registerCommand(singleForm(QStringLiteral("loadtestimony"), {cm_loadtestimony}, {l_name}, QStringLiteral("Loads a saved testimony for playback."), cmdLoadTestimony), l_core);
}

} // namespace akashi::commands
