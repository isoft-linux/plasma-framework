#include <lunar-date/lunar-date.h>
#include <stdio.h>
#include <locale.h>
#include <lunar.h>
#include <QVariant>
#include <klocalizedstring.h>

Lunar::Lunar(QObject *parent)
    : QObject(parent), m_year(-1), m_month(-1), m_day(-1)
{}

Lunar::~Lunar()
{}

void Lunar::setYear(int y)
{
    m_year = y;
    emit yearChanged();
    get();
    m_text = "okokok";
}

void Lunar::setMonth(int m)
{
    m_month = m + 1;
    emit monthChanged();
    get();
}

void Lunar::setDay(int d)
{
    m_day = d;
    emit dayChanged();
    get();
}

void Lunar::setFestival(const QString &fest)
{
    m_festival = fest;
    emit festivalChanged();
}

void Lunar::setText(const QString &text)
{
    m_text = text;
    emit textChanged();
}

QString Lunar::festival() const
{
    return m_festival;
}

QString Lunar::text() const
{
    return m_text;
}

int Lunar::year() const
{
    return m_year;
}

int Lunar::month() const
{
    return m_month;
}

int Lunar::day() const
{
    return m_day;
}

void Lunar::get()
{
    if (m_year < 0 || m_month < 0 || m_day < 0)
    {
        m_festival.clear();
        m_text.clear();
        return;
    }

    GError *err = NULL;
    LunarDate *date = lunar_date_new();
    lunar_date_set_solar_date(date, m_year, (GDateMonth)m_month, m_day, 0, &err);

    m_festival = lunar_date_get_jieri(date, "\n");
    QString format = i18n("Lunar: %(NIAN)(Y)%(YUE)(M)%(RI)(D)");
    format += "\n" + m_festival;
    m_text = lunar_date_strftime(date, format.toStdString().c_str());

    emit festivalChanged();
    emit textChanged();

    lunar_date_free(date);
}
