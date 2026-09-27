#include <QtTest>
#include <QCborMap>
#include <QCborValue>
#include <algorithm>

#include "../ars_tracker_log_utils.h"
#include "../ars_tracker_parser.h"
#include "../ars_tracker_utils.h"
#include "../smp_message.h"
#include "../smp_response_error_decoder.h"
#include "../smp_shell_response_parser.h"

class ArsTrackerLogUtilsTests : public QObject
{
    Q_OBJECT

private slots:
    void shellResultRequiresCompleteValidZeroRet();
    void supportCheckStateAndDeferral();
    void supportProbeSuccessfulSequence();
    void supportProbeStopsWhenInitialResetFails();
    void supportProbeStopsWhenLogsDirectoryFails();
    void supportProbeContinuesWhenFinalResetFails();
    void supportProbeKeepsAvailableWhenPrepareFails();
    void supportProbeRejectsDisconnectedOrStaleContext();
    void loadPreparesFreshLogsBeforeListing();
    void supportContextRejectsLateGeneration();
    void emptyMeasLsIsValid();
    void parsesModernAndLegacyShellReturnCodes();
    void shellReturnCodeDoesNotLeakAcrossResponses();
    void distinguishesNestedAndLegacyManagementErrors();
    void parsesRealListingAndFiltersEntries();
    void rejectsInvalidListing();
    void ordersPlainAndWrappedSequences();
    void ordersMoreThanTenSparseFiles();
    void preservesBytesAcrossFileBoundaries();
    void marksPartialHistory();
    void sortsEmptyAndSingleTrackerLists();
    void sortsTrackersByPairAndSide();
    void sortsNumericPairIdsNumerically();
    void sortsInvalidTrackersByFallback();
};

struct tracker_sort_test_item_t
{
    QString name;
    QString serial;
    QString port;
};

static void sort_trackers(QList<tracker_sort_test_item_t> *items)
{
    std::stable_sort(items->begin(), items->end(),
                     [](const tracker_sort_test_item_t &left,
                        const tracker_sort_test_item_t &right) {
        return ars_tracker_utils::tracker_pair_less(
                left.name, left.serial, left.port,
                right.name, right.serial, right.port);
    });
}

static QByteArray cbor(const QCborMap &map)
{
    return QCborValue(map).toCbor();
}

void ArsTrackerLogUtilsTests::shellResultRequiresCompleteValidZeroRet()
{
    QVERIFY(ars_tracker_log_utils::shell_result_is_success(true, true, 0));
    QVERIFY(!ars_tracker_log_utils::shell_result_is_success(true, true, -8));
    QVERIFY(!ars_tracker_log_utils::shell_result_is_success(true, false, 0));
    QVERIFY(!ars_tracker_log_utils::shell_result_is_success(false, true, 0));
}

void ArsTrackerLogUtilsTests::supportCheckStateAndDeferral()
{
    QVERIFY(ars_tracker_log_utils::support_check_should_defer(true, false, false, false));
    QVERIFY(ars_tracker_log_utils::support_check_should_defer(false, true, false, false));
    QVERIFY(ars_tracker_log_utils::support_check_should_defer(false, false, true, false));
    QVERIFY(ars_tracker_log_utils::support_check_should_defer(false, false, false, true));
    QVERIFY(!ars_tracker_log_utils::support_check_should_defer(false, false, false, false));
}

void ArsTrackerLogUtilsTests::supportProbeSuccessfulSequence()
{
    using namespace ars_tracker_log_utils;
    support_flow_transition_t step = support_flow_after_callback(
            SUPPORT_FLOW_RESET_CWD_BEFORE_PROBE, true);
    QCOMPARE(step.next_state, SUPPORT_FLOW_CHECK_LOGS_DIRECTORY);
    QCOMPARE(step.next_command, SUPPORT_COMMAND_CHECK_LOGS_DIRECTORY);
    QVERIFY(!step.mark_available);

    step = support_flow_after_callback(step.next_state, true);
    QCOMPARE(step.next_state, SUPPORT_FLOW_RESET_CWD_AFTER_PROBE);
    QCOMPARE(step.next_command, SUPPORT_COMMAND_RESET_CWD_AFTER_PROBE);
    QVERIFY(step.mark_available);

    step = support_flow_after_callback(step.next_state, true);
    QCOMPARE(step.next_state, SUPPORT_FLOW_PREPARE_LOGS);
    QCOMPARE(step.next_command, SUPPORT_COMMAND_PREPARE_LOGS);
    QVERIFY(step.mark_available);

    step = support_flow_after_callback(step.next_state, true);
    QCOMPARE(step.next_state, SUPPORT_FLOW_FINISHED);
    QCOMPARE(step.next_command, SUPPORT_COMMAND_NONE);
    QVERIFY(step.mark_available);
}

void ArsTrackerLogUtilsTests::supportProbeStopsWhenInitialResetFails()
{
    using namespace ars_tracker_log_utils;
    const bool reset_ok = shell_result_is_success(false, true, 0);
    const support_flow_transition_t step = support_flow_after_callback(
            SUPPORT_FLOW_RESET_CWD_BEFORE_PROBE, reset_ok);
    QCOMPARE(step.next_state, SUPPORT_FLOW_FINISHED);
    QCOMPARE(step.next_command, SUPPORT_COMMAND_NONE);
    QVERIFY(step.mark_unsupported);
}

void ArsTrackerLogUtilsTests::supportProbeStopsWhenLogsDirectoryFails()
{
    using namespace ars_tracker_log_utils;
    const smp_shell_execute_response_t legacy_error =
            smp_shell_response_parser::parse_execute_response(
                    cbor({{QStringLiteral("o"), QStringLiteral("not found")},
                          {QStringLiteral("rc"), -2}}));
    const bool cd_ok = shell_result_is_success(
            true, legacy_error.ret_valid, legacy_error.ret);
    const support_flow_transition_t step = support_flow_after_callback(
            SUPPORT_FLOW_CHECK_LOGS_DIRECTORY, cd_ok);
    QCOMPARE(step.next_state, SUPPORT_FLOW_FINISHED);
    QCOMPARE(step.next_command, SUPPORT_COMMAND_NONE);
    QVERIFY(step.mark_unsupported);
}

void ArsTrackerLogUtilsTests::supportProbeContinuesWhenFinalResetFails()
{
    using namespace ars_tracker_log_utils;
    const support_flow_transition_t step = support_flow_after_callback(
            SUPPORT_FLOW_RESET_CWD_AFTER_PROBE, false);
    QCOMPARE(step.next_state, SUPPORT_FLOW_PREPARE_LOGS);
    QCOMPARE(step.next_command, SUPPORT_COMMAND_PREPARE_LOGS);
    QVERIFY(step.mark_available);
    QVERIFY(!step.mark_unsupported);
}

void ArsTrackerLogUtilsTests::supportProbeKeepsAvailableWhenPrepareFails()
{
    using namespace ars_tracker_log_utils;
    const support_flow_transition_t step = support_flow_after_callback(
            SUPPORT_FLOW_PREPARE_LOGS, false);
    QCOMPARE(step.next_state, SUPPORT_FLOW_FINISHED);
    QCOMPARE(step.next_command, SUPPORT_COMMAND_NONE);
    QVERIFY(step.mark_available);
    QVERIFY(!step.mark_unsupported);
}

void ArsTrackerLogUtilsTests::supportProbeRejectsDisconnectedOrStaleContext()
{
    using namespace ars_tracker_log_utils;
    const QList<support_flow_state_t> states = {
        SUPPORT_FLOW_RESET_CWD_BEFORE_PROBE,
        SUPPORT_FLOW_CHECK_LOGS_DIRECTORY,
        SUPPORT_FLOW_RESET_CWD_AFTER_PROBE,
        SUPPORT_FLOW_PREPARE_LOGS,
    };
    for (support_flow_state_t state : states)
    {
        const support_flow_transition_t step = support_flow_after_callback(state, true, false);
        QCOMPARE(step.next_state, SUPPORT_FLOW_FINISHED);
        QCOMPARE(step.next_command, SUPPORT_COMMAND_NONE);
        QVERIFY(!step.mark_available);
        QVERIFY(!step.mark_unsupported);
    }
}

void ArsTrackerLogUtilsTests::loadPreparesFreshLogsBeforeListing()
{
    QVERIFY(ars_tracker_log_utils::load_should_start_listing(true, true));
    QVERIFY(!ars_tracker_log_utils::load_should_start_listing(false, true));
    QVERIFY(!ars_tracker_log_utils::load_should_start_listing(true, false));
}

void ArsTrackerLogUtilsTests::supportContextRejectsLateGeneration()
{
    QVERIFY(ars_tracker_log_utils::support_context_matches(
            "COM24", "ARS.1.2.00000035", 7,
            "COM24", "ARS.1.2.00000035", 7));
    QVERIFY(!ars_tracker_log_utils::support_context_matches(
            "COM24", "ARS.1.2.00000035", 8,
            "COM24", "ARS.1.2.00000035", 7));
    QVERIFY(!ars_tracker_log_utils::support_context_matches(
            "COM25", "ARS.1.2.00000035", 7,
            "COM24", "ARS.1.2.00000035", 7));
}

void ArsTrackerLogUtilsTests::emptyMeasLsIsValid()
{
    QList<ars_tracker_session_t> sessions;
    QString error = QStringLiteral("stale error");
    QVERIFY2(ars_tracker_parser::parse_meas_ls_output("\r\n", &sessions, &error),
             qPrintable(error));
    QVERIFY(sessions.isEmpty());
    QVERIFY(error.isEmpty());
}

void ArsTrackerLogUtilsTests::parsesModernAndLegacyShellReturnCodes()
{
    smp_shell_execute_response_t response =
            smp_shell_response_parser::parse_execute_response(
                    cbor({{QStringLiteral("o"), QString()},
                          {QStringLiteral("ret"), 0}}));
    QVERIFY(response.valid);
    QVERIFY(response.ret_valid);
    QCOMPARE(response.ret, 0);

    response = smp_shell_response_parser::parse_execute_response(
            cbor({{QStringLiteral("o"), QString()},
                  {QStringLiteral("rc"), 0}}));
    QVERIFY(response.valid);
    QVERIFY(response.ret_valid);
    QCOMPARE(response.ret, 0);

    response = smp_shell_response_parser::parse_execute_response(
            cbor({{QStringLiteral("o"), QStringLiteral("failed")},
                  {QStringLiteral("rc"), -7}}));
    QVERIFY(response.valid);
    QVERIFY(response.ret_valid);
    QCOMPARE(response.ret, -7);
    QCOMPARE(response.output, QStringLiteral("failed"));
    smp_error_t error;
    QVERIFY(smp_response_error_decoder::decode(
            cbor({{QStringLiteral("o"), QStringLiteral("failed")},
                  {QStringLiteral("rc"), -7}}),
            1, 9, SMP_OP_WRITE_RESPONSE, 0, &error));
    QCOMPARE(error.type, SMP_ERROR_NONE);

    response = smp_shell_response_parser::parse_execute_response(
            cbor({{QStringLiteral("o"), QString()},
                  {QStringLiteral("rc"), -4},
                  {QStringLiteral("ret"), 3}}));
    QVERIFY(response.ret_valid);
    QCOMPARE(response.ret, 3);
}

void ArsTrackerLogUtilsTests::shellReturnCodeDoesNotLeakAcrossResponses()
{
    const smp_shell_execute_response_t first =
            smp_shell_response_parser::parse_execute_response(
                    cbor({{QStringLiteral("o"), QString()},
                          {QStringLiteral("rc"), 0}}));
    const smp_shell_execute_response_t second =
            smp_shell_response_parser::parse_execute_response(
                    cbor({{QStringLiteral("o"), QStringLiteral("next")}}));
    QVERIFY(first.ret_valid);
    QVERIFY(second.valid);
    QVERIFY(!second.ret_valid);
    QCOMPARE(second.ret, 0);
}

void ArsTrackerLogUtilsTests::distinguishesNestedAndLegacyManagementErrors()
{
    const QByteArray nested_error = cbor({
            {QStringLiteral("err"), QCborMap({
                    {QStringLiteral("group"), 9},
                    {QStringLiteral("rc"), 2}})}});
    smp_error_t error;
    QVERIFY(smp_response_error_decoder::decode(
            nested_error, 1, 9, SMP_OP_WRITE_RESPONSE, 0, &error));
    QCOMPARE(error.type, SMP_ERROR_RET);
    QCOMPARE(error.group, 9);
    QCOMPARE(error.rc, 2);

    const smp_shell_execute_response_t shell =
            smp_shell_response_parser::parse_execute_response(nested_error);
    QVERIFY(shell.valid);
    QVERIFY(!shell.ret_valid);

    QVERIFY(smp_response_error_decoder::decode(
            cbor({{QStringLiteral("rc"), 5}}), 1, 8,
            SMP_OP_WRITE_RESPONSE, 0, &error));
    QCOMPARE(error.type, SMP_ERROR_RC);
    QCOMPARE(error.rc, 5);
}

void ArsTrackerLogUtilsTests::parsesRealListingAndFiltersEntries()
{
    const QString listing = QString::fromLatin1(
            "\x1b[32mlog.9998\x1b[0m\r\n"
            "notes.txt\r\n"
            "subdir/\r\n"
            "log.0001\r\n"
            "log.9999\r\n"
            "log.0000\r\n"
            "log.0017\r\n"
            "log.0017\r\n");
    QStringList files;
    QString error;
    QVERIFY2(ars_tracker_log_utils::parse_listing(listing, &files, &error),
             qPrintable(error));
    QCOMPARE(files, QStringList({"log.9998", "log.9999", "log.0000", "log.0001",
                                 "log.0017"}));

    QVERIFY(ars_tracker_log_utils::parse_listing(QString(), &files, &error));
    QVERIFY(files.isEmpty());
}

void ArsTrackerLogUtilsTests::rejectsInvalidListing()
{
    QStringList files;
    QString error;
    QVERIFY(!ars_tracker_log_utils::parse_listing("fs ls NAND:/logs\r\nars>\r\n",
                                                  &files, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!ars_tracker_log_utils::parse_listing(
            QString::fromLatin1("log.0001\0truncated", 18), &files, &error));
}


void ArsTrackerLogUtilsTests::ordersPlainAndWrappedSequences()
{
    QCOMPARE(ars_tracker_log_utils::order_log_files(
                     {"log.0042", "log.0040", "log.0041"}),
             QStringList({"log.0040", "log.0041", "log.0042"}));
    QCOMPARE(ars_tracker_log_utils::order_log_files(
                     {"log.9998", "log.0001", "log.9999", "log.0000"}),
             QStringList({"log.9998", "log.9999", "log.0000", "log.0001"}));
    QCOMPARE(ars_tracker_log_utils::order_log_files(
                     {"log.9997", "log.0002", "log.9999"}),
             QStringList({"log.9997", "log.9999", "log.0002"}));
}

void ArsTrackerLogUtilsTests::ordersMoreThanTenSparseFiles()
{
    QStringList files;
    for (int number = 9910; number <= 9999; ++number)
    {
        files.append(QStringLiteral("log.%1").arg(number, 4, 10, QLatin1Char('0')));
    }
    for (int number = 0; number < 20; ++number)
    {
        files.append(QStringLiteral("log.%1").arg(number, 4, 10, QLatin1Char('0')));
    }
    const QStringList ordered = ars_tracker_log_utils::order_log_files(files);
    QCOMPARE(ordered.size(), files.size());
    QCOMPARE(ordered.first(), QStringLiteral("log.9910"));
    QVERIFY(ordered.contains(QStringLiteral("log.9999")));
    QVERIFY(ordered.contains(QStringLiteral("log.0019")));
}

void ArsTrackerLogUtilsTests::preservesBytesAcrossFileBoundaries()
{
    QByteArray history;
    const QByteArray utf8 = QString::fromUtf8("До свидания").toUtf8();
    ars_tracker_log_utils::append_downloaded_file(&history, utf8.left(utf8.size() - 1));
    ars_tracker_log_utils::append_downloaded_file(&history, utf8.right(1));
    QCOMPARE(history, utf8);

    QByteArray large(700 * 1024, 'x');
    history.clear();
    ars_tracker_log_utils::append_downloaded_file(&history, large);
    QCOMPARE(history, large);
}

void ArsTrackerLogUtilsTests::marksPartialHistory()
{
    QByteArray history("left");
    ars_tracker_log_utils::append_downloaded_file(
            &history, QByteArray("right"), {"log.0041", "log.0042"});
    QVERIFY(history.startsWith("left\n[Incomplete log history: missing log.0041, log.0042]\n"));
    QVERIFY(history.endsWith("right"));
}

void ArsTrackerLogUtilsTests::sortsTrackersByPairAndSide()
{
    QList<tracker_sort_test_item_t> items = {
        {"00038L", "ARS.1.2.00038", "COM4"},
        {"00033L", "ARS.1.2.00033", "COM2"},
        {"00038R", "ARS.1.1.00038", "COM3"},
        {"00033R", "ARS.1.1.00033", "COM1"},
    };
    sort_trackers(&items);
    QCOMPARE(items.at(0).name, QString("00033R"));
    QCOMPARE(items.at(1).name, QString("00033L"));
    QCOMPARE(items.at(2).name, QString("00038R"));
    QCOMPARE(items.at(3).name, QString("00038L"));
}

void ArsTrackerLogUtilsTests::sortsEmptyAndSingleTrackerLists()
{
    QList<tracker_sort_test_item_t> items;
    sort_trackers(&items);
    QVERIFY(items.isEmpty());

    items.append({"00007R", "ARS.1.1.00007", "COM7"});
    sort_trackers(&items);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.constFirst().port, QString("COM7"));
}

void ArsTrackerLogUtilsTests::sortsNumericPairIdsNumerically()
{
    QList<tracker_sort_test_item_t> items = {
        {"10L", "ARS.1.2.10", "COM4"},
        {"2L", "ARS.1.2.2", "COM2"},
        {"00010R", "ARS.1.1.00010", "COM3"},
        {"00002R", "ARS.1.1.00002", "COM1"},
    };
    sort_trackers(&items);
    QCOMPARE(items.at(0).name, QString("00002R"));
    QCOMPARE(items.at(1).name, QString("2L"));
    QCOMPARE(items.at(2).name, QString("00010R"));
    QCOMPARE(items.at(3).name, QString("10L"));
}

void ArsTrackerLogUtilsTests::sortsInvalidTrackersByFallback()
{
    QList<tracker_sort_test_item_t> items = {
        {"Zulu", QString(), "COM9"},
        {"Alpha", "invalid-b", "COM8"},
        {"Alpha", "invalid-a", "COM9"},
        {"Alpha", "invalid-b", "COM7"},
        {"Only right", "ARS.1.1.5", "COM7"},
        {"Beta", "ARS.1.9.1", "COM6"},
    };
    sort_trackers(&items);
    QCOMPARE(items.at(0).name, QString("Only right"));
    QCOMPARE(items.at(1).name, QString("Alpha"));
    QCOMPARE(items.at(1).serial, QString("invalid-a"));
    QCOMPARE(items.at(2).port, QString("COM7"));
    QCOMPARE(items.at(3).port, QString("COM8"));
    QCOMPARE(items.at(4).name, QString("Beta"));
    QCOMPARE(items.at(5).name, QString("Zulu"));
}

QTEST_APPLESS_MAIN(ArsTrackerLogUtilsTests)
#include "ars_tracker_log_utils_tests.moc"
