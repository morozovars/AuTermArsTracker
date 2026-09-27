#include <QtTest>
#include <QCborMap>
#include <QCborValue>

#include "../ars_tracker_log_utils.h"
#include "../ars_tracker_parser.h"
#include "../smp_message.h"
#include "../smp_response_error_decoder.h"
#include "../smp_shell_response_parser.h"

class ArsTrackerLogUtilsTests : public QObject
{
    Q_OBJECT

private slots:
    void shellResultRequiresCompleteValidZeroRet();
    void supportCheckStateAndDeferral();
    void supportContextRejectsLateGeneration();
    void emptyMeasLsIsValid();
    void parsesModernAndLegacyShellReturnCodes();
    void shellReturnCodeDoesNotLeakAcrossResponses();
    void distinguishesNestedAndLegacyManagementErrors();
    void supportCheckLegacyScenarios();
    void parsesRealListingAndFiltersEntries();
    void rejectsInvalidListing();
    void ordersPlainAndWrappedSequences();
    void ordersMoreThanTenSparseFiles();
    void preservesBytesAcrossFileBoundaries();
    void marksPartialHistory();
};

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
    QCOMPARE(ars_tracker_log_utils::support_state_after_cd(true, true, 0), 1);
    QCOMPARE(ars_tracker_log_utils::support_state_after_cd(true, true, -1), 2);
    QCOMPARE(ars_tracker_log_utils::support_state_after_cd(true, false, 0), 0);
    QCOMPARE(ars_tracker_log_utils::support_state_after_cd(false, true, 0), 0);
    QVERIFY(ars_tracker_log_utils::support_check_should_defer(true, false, false, false));
    QVERIFY(ars_tracker_log_utils::support_check_should_defer(false, true, false, false));
    QVERIFY(ars_tracker_log_utils::support_check_should_defer(false, false, true, false));
    QVERIFY(ars_tracker_log_utils::support_check_should_defer(false, false, false, true));
    QVERIFY(!ars_tracker_log_utils::support_check_should_defer(false, false, false, false));
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

void ArsTrackerLogUtilsTests::supportCheckLegacyScenarios()
{
    const smp_shell_execute_response_t pwd_root =
            smp_shell_response_parser::parse_execute_response(
                    cbor({{QStringLiteral("o"), QStringLiteral("/\r\n")},
                          {QStringLiteral("rc"), 0}}));
    QCOMPARE(ars_tracker_log_utils::extract_absolute_cwd(pwd_root.output),
             QStringLiteral("/"));
    const smp_shell_execute_response_t cd_ok =
            smp_shell_response_parser::parse_execute_response(
                    cbor({{QStringLiteral("o"), QString()},
                          {QStringLiteral("rc"), 0}}));
    QCOMPARE(ars_tracker_log_utils::support_state_after_cd(
                     true, cd_ok.ret_valid, cd_ok.ret), 1);
    const smp_shell_execute_response_t restore_ok =
            smp_shell_response_parser::parse_execute_response(
                    cbor({{QStringLiteral("o"), QString()},
                          {QStringLiteral("rc"), 0}}));
    QVERIFY(restore_ok.valid);
    QVERIFY(restore_ok.ret_valid);
    QCOMPARE(restore_ok.ret, 0);

    const smp_shell_execute_response_t pwd_logs =
            smp_shell_response_parser::parse_execute_response(
                    cbor({{QStringLiteral("o"),
                           QStringLiteral("\x1b[32m/NAND:/logs\x1b[0m\r\n")},
                          {QStringLiteral("rc"), 0}}));
    QCOMPARE(ars_tracker_log_utils::extract_absolute_cwd(pwd_logs.output),
             QStringLiteral("/NAND:/logs"));

    const smp_shell_execute_response_t cd_missing =
            smp_shell_response_parser::parse_execute_response(
                    cbor({{QStringLiteral("o"), QStringLiteral("not found")},
                          {QStringLiteral("rc"), -2}}));
    QCOMPARE(ars_tracker_log_utils::support_state_after_cd(
                     true, cd_missing.ret_valid, cd_missing.ret), 2);
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

QTEST_APPLESS_MAIN(ArsTrackerLogUtilsTests)
#include "ars_tracker_log_utils_tests.moc"
