#ifndef LUNAR_H
#define LUNAR_H

#include <QObject>
#include <QString>

class Lunar : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int year READ year WRITE setYear NOTIFY yearChanged)
    Q_PROPERTY(int month READ month WRITE setMonth NOTIFY monthChanged)
    Q_PROPERTY(int day READ day WRITE setDay NOTIFY dayChanged)
    Q_PROPERTY(QString festival READ festival WRITE setFestival NOTIFY festivalChanged)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)

public:
    Lunar(QObject *parent = 0);
    ~Lunar();

    void setYear(int y);
    void setMonth(int m);
    void setDay(int d);
    void setFestival(const QString &fest);
    void setText(const QString &text);

    int year() const;
    int month() const;
    int day() const;
    QString festival() const;
    QString text() const;

Q_SIGNALS:
    void yearChanged();
    void monthChanged();
    void dayChanged();
    void festivalChanged();
    void textChanged();

private:
    void get();

private:
    QString m_festival;
    QString m_text;
    int m_year;
    int m_month;
    int m_day;
};
#endif
