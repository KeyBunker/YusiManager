#include "text_size.h"
#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QFontInfo>
#include <QSettings>
#include <cmath>
#include <utility>

namespace {
class FontChangeObserver : public QObject {
public:
    FontChangeObserver(QObject *context, std::function<void()> callback)
        : QObject(context), m_callback(std::move(callback)) {
        qApp->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::ApplicationFontChange) m_callback();
        return false;
    }
private:
    std::function<void()> m_callback;
};
}

void TextSize::onChange(QObject *context, std::function<void()> callback) {
    new FontChangeObserver(context, std::move(callback));
}

bool TextSize::valid(double size) {
    return std::isfinite(size) && size >= Minimum && size <= Maximum;
}

double TextSize::current() {
    const QFont font = QApplication::font();
    return font.pointSizeF() > 0 ? font.pointSizeF() : QFontInfo(font).pointSizeF();
}

void TextSize::load() {
    set(QSettings().value("textSize").toDouble(), false);
}

bool TextSize::set(double size, bool persist) {
    if (!valid(size)) return false;
    if (persist) {
        QSettings settings;
        settings.setValue("textSize", size);
        settings.sync();
        if (settings.status() != QSettings::NoError) return false;
    }
    QFont font = QApplication::font();
    font.setPointSizeF(size);
    QApplication::setFont(font);
    return true;
}
