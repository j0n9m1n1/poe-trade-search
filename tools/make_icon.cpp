// SPDX-License-Identifier: GPL-3.0-or-later
//
// resources/app.svg -> app.ico (16~256 PNG 묶음) + app.png (256)
//   QT_QPA_PLATFORM=offscreen make_icon resources/app.svg resources/app.ico resources/app.png
// SVG 를 읽으려면 Qt SVG 이미지 플러그인(qt6-svg-plugins / Qt 설치본 imageformats/qsvg)이 있어야 함
#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QTextStream>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QTextStream err(stderr);
    if (argc < 4) {
        err << "usage: make_icon in.svg out.ico out.png\n";
        return 1;
    }
    const QString in = QString::fromLocal8Bit(argv[1]);
    const QList<int> sizes = {16, 20, 24, 32, 40, 48, 64, 128, 256};

    QList<QByteArray> pngs;
    for (int s : sizes) {
        QImageReader r(in);
        r.setScaledSize(QSize(s, s));
        const QImage img = r.read().convertToFormat(QImage::Format_ARGB32);
        if (img.isNull()) {
            err << "cannot render " << in << " at " << s << ": " << r.errorString() << "\n";
            return 2;
        }
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        pngs << png;
        if (s == 256)
            img.save(QString::fromLocal8Bit(argv[3]), "PNG");
    }

    // ICO: ICONDIR + ICONDIRENTRY * n + PNG 데이터 (Vista 이후 PNG 엔트리 지원)
    QFile f(QString::fromLocal8Bit(argv[2]));
    if (!f.open(QIODevice::WriteOnly)) {
        err << "cannot write " << f.fileName() << "\n";
        return 3;
    }
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * sizes.size();
    for (int i = 0; i < sizes.size(); ++i) {
        const int s = sizes[i];
        ds << quint8(s >= 256 ? 0 : s) << quint8(s >= 256 ? 0 : s) << quint8(0) << quint8(0) << quint16(1)
           << quint16(32) << quint32(pngs[i].size()) << quint32(offset);
        offset += pngs[i].size();
    }
    for (const QByteArray& p : pngs)
        f.write(p);
    return 0;
}
