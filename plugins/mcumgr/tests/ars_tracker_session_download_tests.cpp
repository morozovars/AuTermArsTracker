#include <QtTest>

#include <QCborMap>
#include <QCborValue>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "../ars_tracker_backend.h"
#include "../ars_trackers_session_download_coordinator.h"
#include "../crc16.h"
#include "../smp_fs_status_response_parser.h"

class ArsTrackerSessionDownloadTests : public QObject
{
    Q_OBJECT

private slots:
    void coordinatorBackendsUseSizeOnlyPolicy();
    void perPortFsStateAddressesRemainStable();
    void sizeOnlyStartsWithStatusAndNoHashRequests();
    void freshDownloadUsesStatusThenDownload();
    void sameSizeFinalFileIsSkipped();
    void shorterFinalFileIsPreparedForResume();
    void completePartIsFinalized();
    void oversizedFinalFileRestartsFromZero();
    void sizeOnlySkipsConsecutiveSensorsAndKeepsProcessedStatusName();
    void statusParserRequiresLen();
    void interleavedBackendsKeepSizesSeparate();
    void disconnectedBackendDoesNotStopAnotherBackend();
    void cancellationAllowsRestartAndIgnoresLateCallback();
    void consecutiveSessionsDoNotWaitForHashSupport();
    void defaultPolicyKeepsHashWorkflow();
    void transportCrc16RemainsAvailable();
};

static void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), qint64(contents.size()));
    QVERIFY(file.flush());
    file.close();
}

static void startExport(ars_tracker_backend *backend, const QString &session,
                        const QString &destination)
{
    QString error;
    QVERIFY2(backend->begin_session_export_explicit(session, destination, &error),
             qPrintable(error));
}

static QList<QVariant> takeMetadataRequest(QSignalSpy *metadata)
{
    for (int attempt = 0; attempt < 100 && metadata->isEmpty(); ++attempt)
    {
        QTest::qWait(1);
    }
    if (metadata->isEmpty())
    {
        QTest::qFail("Expected a metadata request", __FILE__, __LINE__);
        return {QVariant(), QVariant()};
    }
    return metadata->takeFirst();
}

static void finishAllPresentSession(ars_tracker_backend *backend, QSignalSpy *metadata,
                                    const QString &destination)
{
    const QStringList names = {QStringLiteral("trace.csv"),
                               QStringLiteral("processedStr.csv"),
                               QStringLiteral("battery.csv"),
                               QStringLiteral("sensors_0.bin")};
    for (const QString &name : names)
    {
        const QByteArray contents = QByteArray("data-") + name.toUtf8();
        writeFile(QDir(destination).filePath(name), contents);
    }

    for (const QString &name : names)
    {
        const QList<QVariant> request = takeMetadataRequest(metadata);
        QVERIFY(request.at(0).toString().endsWith(name));
        QVERIFY(request.at(1).toString().isEmpty());
        backend->handle_file_metadata_result(STATUS_COMPLETE, QString(), QByteArray(),
                                             uint32_t(QFileInfo(QDir(destination).filePath(name)).size()));
    }

    const QList<QVariant> sensor_end = takeMetadataRequest(metadata);
    QVERIFY(sensor_end.at(0).toString().endsWith(QStringLiteral("sensors_1.bin")));
    backend->handle_file_metadata_result(STATUS_ERROR,
                                         QStringLiteral("The specified file does not exist"),
                                         QByteArray(), 0);
}

static QString sensorName(int index)
{
    return QStringLiteral("sensors_%1.bin").arg(index);
}

static QStringList fixedFileNames()
{
    return {QStringLiteral("trace.csv"),
            QStringLiteral("processedStr.csv"),
            QStringLiteral("battery.csv")};
}

static QStringList allPresentSessionNames(int last_sensor_index)
{
    QStringList names = fixedFileNames();
    for (int index = 0; index <= last_sensor_index; ++index)
    {
        names.append(sensorName(index));
    }
    return names;
}

static void writeExistingSessionFiles(const QString &destination, int last_sensor_index)
{
    const QStringList names = allPresentSessionNames(last_sensor_index);
    for (const QString &name : names)
    {
        const QByteArray contents = QByteArray("existing-") + name.toUtf8();
        writeFile(QDir(destination).filePath(name), contents);
    }
}

static void answerSizeMatch(ars_tracker_backend *backend, QSignalSpy *metadata,
                            QSignalSpy *status, const QString &destination,
                            const QString &expected_name,
                            QStringList *requested_names = nullptr)
{
    const QList<QVariant> request = takeMetadataRequest(metadata);
    const QString requested_file = request.at(0).toString();
    if (requested_names != nullptr)
    {
        requested_names->append(QFileInfo(requested_file).fileName());
    }
    QVERIFY(requested_file.endsWith(expected_name));
    QVERIFY(request.at(1).toString().isEmpty());

    const int status_count_before = status->count();
    backend->handle_file_metadata_result(
        STATUS_COMPLETE, QString(), QByteArray(),
        uint32_t(QFileInfo(QDir(destination).filePath(expected_name)).size()));
    QVERIFY(status->count() > status_count_before);
    const QString latest_status = status->last().at(0).toString();
    QVERIFY2(latest_status.contains(expected_name), qPrintable(latest_status));
    if (expected_name.startsWith(QStringLiteral("sensors_")))
    {
        const int underscore = expected_name.indexOf(QLatin1Char('_'));
        const int dot = expected_name.indexOf(QLatin1Char('.'));
        bool ok = false;
        const int index = expected_name.mid(underscore + 1, dot - underscore - 1).toInt(&ok);
        QVERIFY(ok);
        QVERIFY2(!latest_status.contains(sensorName(index + 1)), qPrintable(latest_status));
    }
}

static void finishExistingSessionWithSensors(ars_tracker_backend *backend, QSignalSpy *metadata,
                                             QSignalSpy *status, QSignalSpy *finished,
                                             const QString &destination,
                                             int last_sensor_index,
                                             QStringList *requested_names = nullptr)
{
    const QStringList names = allPresentSessionNames(last_sensor_index);
    for (const QString &name : names)
    {
        answerSizeMatch(backend, metadata, status, destination, name, requested_names);
    }

    const QList<QVariant> sensor_end = takeMetadataRequest(metadata);
    const QString end_file = sensor_end.at(0).toString();
    if (requested_names != nullptr)
    {
        requested_names->append(QFileInfo(end_file).fileName());
    }
    QCOMPARE(end_file.endsWith(sensorName(last_sensor_index + 1)), true);
    QCOMPARE(sensor_end.at(1).toString(), QString());
    backend->handle_file_metadata_result(STATUS_ERROR,
                                         QStringLiteral("The specified file does not exist"),
                                         QByteArray(), 0);
    QTRY_VERIFY(finished->count() > 0);
    QVERIFY(finished->last().at(0).toBool());
}

void ArsTrackerSessionDownloadTests::coordinatorBackendsUseSizeOnlyPolicy()
{
    ArsTrackersSessionDownloadCoordinator coordinator;
    coordinator.registerLegacyActiveRoute(QStringLiteral("COM11"), QStringLiteral("serial"),
                                          QStringLiteral("tracker"));
    QString error;
    QVERIFY2(coordinator.createLegacyBackendForActiveRoute(&error), qPrintable(error));
    ars_tracker_backend *backend = coordinator.activeLegacyBackendForPort(QStringLiteral("COM11"));
    QVERIFY(backend != nullptr);
    QCOMPARE(backend->existing_file_check_policy(),
             ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
}

void ArsTrackerSessionDownloadTests::perPortFsStateAddressesRemainStable()
{
    ArsTrackersSessionDownloadCoordinator coordinator;
    coordinator.registerLegacyActiveRoute(QStringLiteral("COM21"), QString(), QString());
    QString error;
    QVERIFY(coordinator.beginLegacyFsOperation(QStringLiteral("COM21"), 1,
                                               QStringLiteral("metadata"),
                                               QStringLiteral("same.bin"), QString(), &error));
    ArsTrackersDownloadFsOperationState *first =
        coordinator.legacyFsOperationMutableForPort(QStringLiteral("COM21"));
    QVERIFY(first != nullptr);
    first->sizeResponse = 21;

    coordinator.registerLegacyActiveRoute(QStringLiteral("COM22"), QString(), QString());
    QVERIFY(coordinator.beginLegacyFsOperation(QStringLiteral("COM22"), 1,
                                               QStringLiteral("metadata"),
                                               QStringLiteral("same.bin"), QString(), &error));
    ArsTrackersDownloadFsOperationState *second =
        coordinator.legacyFsOperationMutableForPort(QStringLiteral("COM22"));
    QVERIFY(second != nullptr);
    second->sizeResponse = 2200;

    for (int port = 23; port < 200; ++port)
    {
        const QString name = QStringLiteral("COM%1").arg(port);
        coordinator.registerLegacyActiveRoute(name, QString(), QString());
        QVERIFY(coordinator.beginLegacyFsOperation(name, 1, QStringLiteral("metadata"),
                                                   QStringLiteral("same.bin"), QString(), &error));
    }

    QCOMPARE(first->sizeResponse, uint32_t(21));
    QCOMPARE(second->sizeResponse, uint32_t(2200));
    QCOMPARE(coordinator.legacyFsOperationForPort(QStringLiteral("COM21")).sizeResponse,
             uint32_t(21));
    QCOMPARE(coordinator.legacyFsOperationForPort(QStringLiteral("COM22")).sizeResponse,
             uint32_t(2200));
}

void ArsTrackerSessionDownloadTests::sizeOnlyStartsWithStatusAndNoHashRequests()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ars_tracker_backend backend;
    backend.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    QSignalSpy hash_support(&backend, &ars_tracker_backend::request_file_hash_support);
    QSignalSpy metadata(&backend, &ars_tracker_backend::request_file_metadata);
    startExport(&backend, QStringLiteral("session-a"), directory.path());
    const QList<QVariant> request = takeMetadataRequest(&metadata);
    QCOMPARE(hash_support.count(), 0);
    QVERIFY(request.at(1).toString().isEmpty());
    backend.cancel_all();
    backend.handle_file_metadata_result(STATUS_CANCELLED, QString(), QByteArray(), 0);
}

void ArsTrackerSessionDownloadTests::freshDownloadUsesStatusThenDownload()
{
    QTemporaryDir directory;
    ars_tracker_backend backend;
    backend.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    QSignalSpy metadata(&backend, &ars_tracker_backend::request_file_metadata);
    QSignalSpy download(&backend, &ars_tracker_backend::request_file_download);
    startExport(&backend, QStringLiteral("session"), directory.path());
    takeMetadataRequest(&metadata);
    backend.handle_file_metadata_result(STATUS_COMPLETE, QString(), QByteArray(), 4);
    QTRY_COMPARE(download.count(), 1);
    QVERIFY(download.at(0).at(1).toString().endsWith(QStringLiteral(".trace.csv.part")));
    backend.cancel_all();
    backend.handle_file_download_result(STATUS_CANCELLED, QString());
}

void ArsTrackerSessionDownloadTests::sameSizeFinalFileIsSkipped()
{
    QTemporaryDir directory;
    const QString final_path = directory.filePath(QStringLiteral("trace.csv"));
    writeFile(final_path, QByteArray("same"));
    ars_tracker_backend backend;
    backend.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    QSignalSpy metadata(&backend, &ars_tracker_backend::request_file_metadata);
    QSignalSpy download(&backend, &ars_tracker_backend::request_file_download);
    QSignalSpy rows(&backend, &ars_tracker_backend::export_file_list_changed);
    startExport(&backend, QStringLiteral("session"), directory.path());
    takeMetadataRequest(&metadata);
    backend.handle_file_metadata_result(STATUS_COMPLETE, QString(), QByteArray(), 4);
    QTRY_VERIFY(!metadata.isEmpty());
    QCOMPARE(download.count(), 0);
    QVERIFY(QFileInfo::exists(final_path));
    QVERIFY(rows.last().at(0).toStringList().join('\n').contains(
        QStringLiteral("size match; checksum not checked")));
    backend.cancel_all();
    backend.handle_file_metadata_result(STATUS_CANCELLED, QString(), QByteArray(), 0);
}

void ArsTrackerSessionDownloadTests::shorterFinalFileIsPreparedForResume()
{
    QTemporaryDir directory;
    writeFile(directory.filePath(QStringLiteral("trace.csv")), QByteArray("ab"));
    ars_tracker_backend backend;
    backend.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    QSignalSpy metadata(&backend, &ars_tracker_backend::request_file_metadata);
    QSignalSpy download(&backend, &ars_tracker_backend::request_file_download);
    startExport(&backend, QStringLiteral("session"), directory.path());
    takeMetadataRequest(&metadata);
    backend.handle_file_metadata_result(STATUS_COMPLETE, QString(), QByteArray(), 5);
    QTRY_COMPARE(download.count(), 1);
    const QString part = directory.filePath(QStringLiteral(".trace.csv.part"));
    QCOMPARE(QFileInfo(part).size(), qint64(2));
    QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("trace.csv"))));
    backend.cancel_all();
    backend.handle_file_download_result(STATUS_CANCELLED, QString());
}

void ArsTrackerSessionDownloadTests::completePartIsFinalized()
{
    QTemporaryDir directory;
    const QString part = directory.filePath(QStringLiteral(".trace.csv.part"));
    writeFile(part, QByteArray("done"));
    ars_tracker_backend backend;
    backend.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    QSignalSpy metadata(&backend, &ars_tracker_backend::request_file_metadata);
    QSignalSpy download(&backend, &ars_tracker_backend::request_file_download);
    startExport(&backend, QStringLiteral("session"), directory.path());
    takeMetadataRequest(&metadata);
    backend.handle_file_metadata_result(STATUS_COMPLETE, QString(), QByteArray(), 4);
    QTRY_VERIFY(!metadata.isEmpty());
    QCOMPARE(download.count(), 0);
    QVERIFY(!QFileInfo::exists(part));
    QCOMPARE(QFileInfo(directory.filePath(QStringLiteral("trace.csv"))).size(), qint64(4));
    backend.cancel_all();
    backend.handle_file_metadata_result(STATUS_CANCELLED, QString(), QByteArray(), 0);
}

void ArsTrackerSessionDownloadTests::oversizedFinalFileRestartsFromZero()
{
    QTemporaryDir directory;
    const QString final_path = directory.filePath(QStringLiteral("trace.csv"));
    writeFile(final_path, QByteArray("too-large"));
    ars_tracker_backend backend;
    backend.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    QSignalSpy metadata(&backend, &ars_tracker_backend::request_file_metadata);
    QSignalSpy download(&backend, &ars_tracker_backend::request_file_download);
    startExport(&backend, QStringLiteral("session"), directory.path());
    QVERIFY(QFileInfo::exists(final_path));
    takeMetadataRequest(&metadata);
    backend.handle_file_metadata_result(STATUS_COMPLETE, QString(), QByteArray(), 4);
    QTRY_COMPARE(download.count(), 1);
    QVERIFY(!QFileInfo::exists(final_path));
    QCOMPARE(QFileInfo(download.at(0).at(1).toString()).exists(), false);
    backend.cancel_all();
    backend.handle_file_download_result(STATUS_CANCELLED, QString());
}

void ArsTrackerSessionDownloadTests::sizeOnlySkipsConsecutiveSensorsAndKeepsProcessedStatusName()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    writeExistingSessionFiles(directory.path(), 5);

    ars_tracker_backend backend;
    backend.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    QSignalSpy metadata(&backend, &ars_tracker_backend::request_file_metadata);
    QSignalSpy download(&backend, &ars_tracker_backend::request_file_download);
    QSignalSpy status(&backend, &ars_tracker_backend::status_message);
    QSignalSpy finished(&backend, &ars_tracker_backend::export_finished);

    startExport(&backend, QStringLiteral("session-with-many-sensors"), directory.path());
    QStringList requested_names;
    finishExistingSessionWithSensors(&backend, &metadata, &status, &finished,
                                     directory.path(), 5, &requested_names);
    QCOMPARE(download.count(), 0);

    const QStringList expected_names = allPresentSessionNames(5) << sensorName(6);
    QCOMPARE(requested_names, expected_names);
    QCOMPARE(metadata.count(), 0);

    QTemporaryDir first_dir;
    QTemporaryDir second_dir;
    QVERIFY(first_dir.isValid());
    QVERIFY(second_dir.isValid());
    writeExistingSessionFiles(first_dir.path(), 5);
    writeExistingSessionFiles(second_dir.path(), 5);
    ars_tracker_backend first;
    ars_tracker_backend second;
    first.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    second.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    QSignalSpy first_metadata(&first, &ars_tracker_backend::request_file_metadata);
    QSignalSpy second_metadata(&second, &ars_tracker_backend::request_file_metadata);
    QSignalSpy first_status(&first, &ars_tracker_backend::status_message);
    QSignalSpy second_status(&second, &ars_tracker_backend::status_message);
    QSignalSpy first_finished(&first, &ars_tracker_backend::export_finished);
    QSignalSpy second_finished(&second, &ars_tracker_backend::export_finished);
    startExport(&first, QStringLiteral("parallel-a"), first_dir.path());
    startExport(&second, QStringLiteral("parallel-b"), second_dir.path());

    const QStringList interleaved_names = allPresentSessionNames(5);
    QStringList first_requested_names;
    QStringList second_requested_names;
    for (const QString &name : interleaved_names)
    {
        answerSizeMatch(&first, &first_metadata, &first_status, first_dir.path(), name,
                        &first_requested_names);
        answerSizeMatch(&second, &second_metadata, &second_status, second_dir.path(), name,
                        &second_requested_names);
    }
    QList<QVariant> end_request = takeMetadataRequest(&first_metadata);
    QString end_file = end_request.at(0).toString();
    first_requested_names.append(QFileInfo(end_file).fileName());
    QVERIFY(end_file.endsWith(sensorName(6)));
    first.handle_file_metadata_result(STATUS_ERROR,
                                      QStringLiteral("The specified file does not exist"),
                                      QByteArray(), 0);
    end_request = takeMetadataRequest(&second_metadata);
    end_file = end_request.at(0).toString();
    second_requested_names.append(QFileInfo(end_file).fileName());
    QVERIFY(end_file.endsWith(sensorName(6)));
    second.handle_file_metadata_result(STATUS_ERROR,
                                       QStringLiteral("The specified file does not exist"),
                                       QByteArray(), 0);
    QTRY_COMPARE(first_finished.count(), 1);
    QTRY_COMPARE(second_finished.count(), 1);
    QVERIFY(first_finished.at(0).at(0).toBool());
    QVERIFY(second_finished.at(0).at(0).toBool());
    QCOMPARE(first_requested_names, expected_names);
    QCOMPARE(second_requested_names, expected_names);

    QTemporaryDir next_dir;
    QVERIFY(next_dir.isValid());
    writeExistingSessionFiles(next_dir.path(), 5);
    QSignalSpy next_metadata(&backend, &ars_tracker_backend::request_file_metadata);
    startExport(&backend, QStringLiteral("next-existing-session"), next_dir.path());
    requested_names.clear();
    finishExistingSessionWithSensors(&backend, &next_metadata, &status, &finished,
                                     next_dir.path(), 5, &requested_names);
    QCOMPARE(finished.count(), 2);
    QCOMPARE(requested_names, expected_names);
}

void ArsTrackerSessionDownloadTests::statusParserRequiresLen()
{
    uint32_t size = 99;
    QString error;
    QVERIFY(!smp_fs_status_response_parser::parse_file_size(
        QCborValue(QCborMap()).toCbor(), &size, &error));
    QCOMPARE(size, uint32_t(99));
    QVERIFY(error.contains(QStringLiteral("len")));

    QVERIFY(!smp_fs_status_response_parser::parse_file_size(
        QByteArray::fromHex("a1636c656e"), &size, &error));
    QCOMPARE(size, uint32_t(99));

    QVERIFY(!smp_fs_status_response_parser::parse_file_size(
        QCborValue(QCborMap{{QStringLiteral("len"), QStringLiteral("bad")}}).toCbor(),
        &size, &error));
    QCOMPARE(size, uint32_t(99));

    QVERIFY(smp_fs_status_response_parser::parse_file_size(
        QCborValue(QCborMap{{QStringLiteral("len"), 0}}).toCbor(), &size, &error));
    QCOMPARE(size, uint32_t(0));
}

void ArsTrackerSessionDownloadTests::interleavedBackendsKeepSizesSeparate()
{
    QTemporaryDir first_dir;
    QTemporaryDir second_dir;
    ars_tracker_backend first;
    ars_tracker_backend second;
    first.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    second.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    QSignalSpy first_metadata(&first, &ars_tracker_backend::request_file_metadata);
    QSignalSpy second_metadata(&second, &ars_tracker_backend::request_file_metadata);
    QSignalSpy first_download(&first, &ars_tracker_backend::request_file_download);
    QSignalSpy second_download(&second, &ars_tracker_backend::request_file_download);
    startExport(&first, QStringLiteral("same-session"), first_dir.path());
    startExport(&second, QStringLiteral("same-session"), second_dir.path());
    takeMetadataRequest(&second_metadata);
    takeMetadataRequest(&first_metadata);
    second.handle_file_metadata_result(STATUS_COMPLETE, QString(), QByteArray(), 17);
    first.handle_file_metadata_result(STATUS_COMPLETE, QString(), QByteArray(), 5);
    QTRY_COMPARE(first_download.count(), 1);
    QTRY_COMPARE(second_download.count(), 1);
    QVERIFY(first_download.at(0).at(1).toString().startsWith(first_dir.path()));
    QVERIFY(second_download.at(0).at(1).toString().startsWith(second_dir.path()));
    first.cancel_all();
    second.cancel_all();
    first.handle_file_download_result(STATUS_CANCELLED, QString());
    second.handle_file_download_result(STATUS_CANCELLED, QString());
}

void ArsTrackerSessionDownloadTests::disconnectedBackendDoesNotStopAnotherBackend()
{
    QTemporaryDir failed_dir;
    QTemporaryDir successful_dir;
    ars_tracker_backend failed;
    ars_tracker_backend successful;
    failed.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    successful.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    QSignalSpy failed_metadata(&failed, &ars_tracker_backend::request_file_metadata);
    QSignalSpy successful_metadata(&successful, &ars_tracker_backend::request_file_metadata);
    QSignalSpy failed_finished(&failed, &ars_tracker_backend::export_finished);
    QSignalSpy successful_finished(&successful, &ars_tracker_backend::export_finished);
    startExport(&failed, QStringLiteral("session"), failed_dir.path());
    startExport(&successful, QStringLiteral("session"), successful_dir.path());
    takeMetadataRequest(&failed_metadata);
    failed.handle_file_metadata_result(STATUS_TRANSPORT_DISCONNECTED, QString(), QByteArray(), 0);
    QTRY_COMPARE(failed_finished.count(), 1);
    finishAllPresentSession(&successful, &successful_metadata, successful_dir.path());
    QTRY_COMPARE(successful_finished.count(), 1);
    QVERIFY(successful_finished.at(0).at(0).toBool());
}

void ArsTrackerSessionDownloadTests::cancellationAllowsRestartAndIgnoresLateCallback()
{
    QTemporaryDir directory;
    ars_tracker_backend backend;
    backend.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    QSignalSpy metadata(&backend, &ars_tracker_backend::request_file_metadata);
    QSignalSpy finished(&backend, &ars_tracker_backend::export_finished);
    startExport(&backend, QStringLiteral("first"), directory.path());
    takeMetadataRequest(&metadata);
    backend.cancel_all();
    backend.handle_file_metadata_result(STATUS_CANCELLED, QString(), QByteArray(), 0);
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished.at(0).at(1).toBool());
    backend.handle_file_metadata_result(STATUS_COMPLETE, QString(), QByteArray(), 123);
    QCOMPARE(finished.count(), 1);

    startExport(&backend, QStringLiteral("second"), directory.path());
    const QList<QVariant> request = takeMetadataRequest(&metadata);
    QVERIFY(request.at(0).toString().contains(QStringLiteral("second")));
    QVERIFY(request.at(1).toString().isEmpty());
    backend.cancel_all();
    backend.handle_file_metadata_result(STATUS_CANCELLED, QString(), QByteArray(), 0);
}

void ArsTrackerSessionDownloadTests::consecutiveSessionsDoNotWaitForHashSupport()
{
    QTemporaryDir first_dir;
    QTemporaryDir second_dir;
    ars_tracker_backend backend;
    backend.set_existing_file_check_policy(ars_tracker_backend::ExistingFileCheckPolicy::SizeOnly);
    QSignalSpy hash_support(&backend, &ars_tracker_backend::request_file_hash_support);
    QSignalSpy metadata(&backend, &ars_tracker_backend::request_file_metadata);
    QSignalSpy finished(&backend, &ars_tracker_backend::export_finished);
    startExport(&backend, QStringLiteral("first"), first_dir.path());
    finishAllPresentSession(&backend, &metadata, first_dir.path());
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished.at(0).at(0).toBool());

    startExport(&backend, QStringLiteral("second"), second_dir.path());
    finishAllPresentSession(&backend, &metadata, second_dir.path());
    QTRY_COMPARE(finished.count(), 2);
    QVERIFY(finished.at(1).at(0).toBool());
    QCOMPARE(hash_support.count(), 0);
}

void ArsTrackerSessionDownloadTests::defaultPolicyKeepsHashWorkflow()
{
    QTemporaryDir directory;
    ars_tracker_backend backend;
    QCOMPARE(backend.existing_file_check_policy(),
             ars_tracker_backend::ExistingFileCheckPolicy::HashAndSize);
    QSignalSpy hash_support(&backend, &ars_tracker_backend::request_file_hash_support);
    QSignalSpy metadata(&backend, &ars_tracker_backend::request_file_metadata);
    startExport(&backend, QStringLiteral("inspector-session"), directory.path());
    QCOMPARE(hash_support.count(), 1);
    QCOMPARE(metadata.count(), 0);
    hash_checksum_t hash;
    hash.name = QStringLiteral("crc32");
    backend.handle_export_hash_support_result(STATUS_COMPLETE, QString(), {hash});
    const QList<QVariant> request = takeMetadataRequest(&metadata);
    QCOMPARE(request.at(1).toString(), QStringLiteral("crc32"));
    backend.cancel_all();
    backend.handle_file_metadata_result(STATUS_CANCELLED, QString(), QByteArray(), 0);
}

void ArsTrackerSessionDownloadTests::transportCrc16RemainsAvailable()
{
    const QByteArray payload("123456789");
    QCOMPARE(crc16(&payload, 0, size_t(payload.size()), 0x1021, 0xffff, false),
             uint16_t(0xa69d));
}

QTEST_GUILESS_MAIN(ArsTrackerSessionDownloadTests)
#include "ars_tracker_session_download_tests.moc"
