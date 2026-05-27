// SPDX-License-Identifier: GPL-3.0-or-later
//
// Native-Qt diagram renderer implementation — see diagram_render.h.
// Pure QPainter; NO WebEngine. Ported from the npd_render_qt prototype.

#include "diagram_render.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QLinearGradient>
#include <QFont>
#include <QFontMetrics>
#include <QLineF>
#include <QMap>
#include <QList>
#include <QVector>
#include <QtMath>
#include <algorithm>

namespace DiagramRender {

namespace {

QFont uiFont(int px, bool bold=false){ QFont f("DejaVu Sans"); f.setPixelSize(px); f.setBold(bold); return f; }

QString iconAlias(QString n){ n=n.toLower();
    static const QHash<QString,QString> a={
        {"db","database"},{"sql","database"},{"datastore","database"},{"postgres","database"},{"mysql","database"},
        {"lb","loadbalancer"},{"balancer","loadbalancer"},{"k8s","kubernetes"},{"fn","function"},{"lambda","function"},
        {"svc","service"},{"microservice","service"},{"api","gateway"},{"apigateway","gateway"},{"mq","queue"},
        {"broker","queue"},{"kafka","queue"},{"bucket","storage"},{"s3","storage"},{"blob","storage"},
        {"auth","lock"},{"security","shield"},{"firewall","shield"},{"mail","email"},{"phone","mobile"},
        {"monitor","desktop"},{"pc","desktop"},{"net","globe"},{"network","globe"},{"world","globe"},
        {"cron","timer"},{"schedule","timer"},{"scheduler","timer"},{"clock","timer"},{"notify","bell"},
        {"alert","bell"},{"repo","git"},{"vcs","git"},{"pk","key"},{"primarykey","key"},{"entity","table"},
        {"proc","process"},{"cog","gear"},{"settings","gear"},{"analytics","chart"},{"graph","chart"},
        {"file","document"},{"doc","document"},{"docs","document"},{"redis","cache"},{"memory","cache"},
        {"person","user"},{"customer","user"},{"client","user"},{"app","mobile"},{"folder","folder"},
        {"container","container"},{"docker","container"},{"search","search"},{"web","browser"},
        {"ml","ai"},{"brain","ai"},{"model","ai"},{"llm","ai"},{"neural","ai"},
        {"processor","cpu"},{"graphics","gpu"},{"disk","disk"},{"hdd","disk"},{"ssd","disk"},{"volume","disk"},
        {"backup","backup"},{"snapshot","backup"},{"sync","sync"},{"refresh","sync"},{"replicate","sync"},
        {"upload","upload"},{"download","download"},{"code","code"},{"terminal","terminal"},{"cli","terminal"},{"shell","terminal"},
        {"deploy","rocket"},{"launch","rocket"},{"ship","rocket"},{"bug","bug"},{"test","test"},{"check","test"},{"ci","test"},
        {"payment","card"},{"creditcard","card"},{"billing","card"},{"cart","cart"},{"order","cart"},{"shop","cart"},
        {"team","users"},{"group","users"},{"users","users"},{"admin","users"},{"bank","bank"},{"finance","bank"},
        {"truck","truck"},{"shipping","truck"},{"delivery","truck"},{"package","package"},{"box","package"},{"parcel","package"},
        {"factory","factory"},{"warehouse","factory"},{"plant","factory"},{"robot","robot"},{"bot","robot"},{"agent","robot"},
        {"chat","chat"},{"message","chat"},{"comment","chat"},{"support","chat"},{"wifi","wifi"},{"signal","wifi"},
        {"camera","camera"},{"video","camera"},{"calendar","calendar"},{"event","calendar"},{"date","calendar"},
        {"location","pin"},{"map","pin"},{"geo","pin"},{"region","pin"},{"tag","tag"},{"label","tag"},
        {"star","star"},{"favorite","star"},{"rating","star"},{"warning","warning"},{"info","info"},
        {"link","link"},{"url","link"},{"filter","filter"},{"funnel","filter"},
    };
    return a.value(n,n);
}

QPointF borderPoint(const QRectF &r, QPointF toward) {
    QPointF c = r.center(); qreal dx = toward.x()-c.x(), dy = toward.y()-c.y();
    if (qFuzzyIsNull(dx) && qFuzzyIsNull(dy)) return c;
    qreal hw = r.width()/2.0, hh = r.height()/2.0;
    qreal sx = dx ? hw/std::abs(dx) : 1e9, sy = dy ? hh/std::abs(dy) : 1e9;
    qreal s = std::min(sx, sy);
    return QPointF(c.x()+dx*s, c.y()+dy*s);
}

QSizeF sizeFor(const Npd::Node &n, const QFontMetrics &fm) {
    const qreal padX=24, minW=104;
    QString t=n.label.isEmpty()?n.id:n.label;
    qreal w=std::max(minW, fm.horizontalAdvance(t)+padX*2.0), h=54;
    if(n.shape==Npd::Shape::Decision){ w+=34; h=70; }
    if(n.shape==Npd::Shape::Icon){ w=std::max<qreal>(w,120); h=88; }
    if(n.shape==Npd::Shape::Database){ h=64; }
    return {w,h};
}

} // namespace

Palette palette(const QString &n) {
    if (n=="ocean")  return {QColor("#0e1a29"),QColor("#193149"),QColor("#13283c"),QColor("#3f86c4"),
                             QColor("#eaf3fb"),QColor("#9cc2e0"),QColor("#5aa0d6"),QColor("#7cc4ff"),QColor("#cfe6ff")};
    if (n=="forest") return {QColor("#0e1f15"),QColor("#1a3725"),QColor("#13301f"),QColor("#3f955f"),
                             QColor("#e9f7ed"),QColor("#a6d4b6"),QColor("#5aa873"),QColor("#86d6a0"),QColor("#c8edd3")};
    if (n=="clay")   return {QColor("#241712"),QColor("#3e2a21"),QColor("#33211a"),QColor("#c97f5e"),
                             QColor("#f8ede7"),QColor("#e2b39c"),QColor("#cf8a68"),QColor("#e8a886"),QColor("#f4d2c1")};
    if (n=="mono")   return {QColor("#15171a"),QColor("#262a30"),QColor("#1d2025"),QColor("#6e7884"),
                             QColor("#eef0f3"),QColor("#aeb6c0"),QColor("#8a939f"),QColor("#aab3bf"),QColor("#d4dae0")};
    return {QColor("#12151b"),QColor("#212835"),QColor("#1a1f29"),QColor("#4d6080"),
            QColor("#e7ecf3"),QColor("#9fadc4"),QColor("#6478a0"),QColor("#8fa6cc"),QColor("#cad6ea")};
}

void drawIcon(QPainter &p, const QString &rawName, const QRectF &b, const QColor &c) {
    const QString name=iconAlias(rawName);
    p.save();
    QPen pen(c, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); p.setPen(pen); p.setBrush(Qt::NoBrush);
    const qreal x=b.x(), y=b.y(), w=b.width(), h=b.height(), cx=b.center().x(), cy=b.center().y();
    auto N=[&](const QString&s){ return name==s; };
    auto dot=[&](QPointF q,qreal r){ p.setBrush(c); p.drawEllipse(q,r,r); p.setBrush(Qt::NoBrush); };
    if (N("database")) { qreal e=h*0.22;
        p.drawEllipse(QRectF(x,y,w,e)); p.drawLine(QPointF(x,y+e/2),QPointF(x,y+h-e/2));
        p.drawLine(QPointF(x+w,y+e/2),QPointF(x+w,y+h-e/2));
        QPainterPath arc; arc.moveTo(x,y+h-e/2); arc.arcTo(QRectF(x,y+h-e,w,e),180,180); p.drawPath(arc);
        p.drawArc(QRectF(x,y+h*0.36,w,e),180,180);
    } else if (N("table")) { p.drawRect(QRectF(x,y,w,h)); p.drawLine(QPointF(x,y+h/3),QPointF(x+w,y+h/3));
        p.drawLine(QPointF(x,y+2*h/3),QPointF(x+w,y+2*h/3)); p.drawLine(QPointF(x+w/3,y),QPointF(x+w/3,y+h));
    } else if (N("server")) { for (int i=0;i<3;i++){ QRectF r(x,y+i*h/3.0,w,h/3.0-3); p.drawRoundedRect(r,2,2); dot(QPointF(x+w-7,r.center().y()),1.6);}
    } else if (N("user")||N("patient")) { p.drawEllipse(QPointF(cx,y+h*0.27),w*0.20,w*0.20);
        QPainterPath body; body.moveTo(x+w*0.16,y+h); body.arcTo(QRectF(x+w*0.16,y+h*0.50,w*0.68,h*0.9),180,-180); p.drawPath(body);
        if (N("patient")){ p.drawLine(QPointF(cx,y+h*0.62),QPointF(cx,y+h*0.82)); p.drawLine(QPointF(cx-w*0.1,y+h*0.72),QPointF(cx+w*0.1,y+h*0.72)); }
    } else if (N("hospital")) { p.drawRect(QRectF(x+w*0.1,y+h*0.25,w*0.8,h*0.75));
        p.drawLine(QPointF(cx,y+h*0.38),QPointF(cx,y+h*0.62)); p.drawLine(QPointF(cx-h*0.12,y+h*0.5),QPointF(cx+h*0.12,y+h*0.5));
    } else if (N("document")) { QPainterPath d; d.moveTo(x+w*0.2,y); d.lineTo(x+w*0.65,y); d.lineTo(x+w*0.8,y+h*0.18);
        d.lineTo(x+w*0.8,y+h); d.lineTo(x+w*0.2,y+h); d.closeSubpath(); p.drawPath(d);
        for(int i=1;i<=3;i++) p.drawLine(QPointF(x+w*0.3,y+h*(0.25*i+0.15)),QPointF(x+w*0.7,y+h*(0.25*i+0.15)));
    } else if (N("cloud")) { QPainterPath cl; cl.addEllipse(QPointF(x+w*0.35,cy),w*0.2,w*0.2); cl.addEllipse(QPointF(x+w*0.6,cy-h*0.05),w*0.24,w*0.24);
        cl.addRect(QRectF(x+w*0.3,cy,w*0.42,h*0.22)); p.drawPath(cl.simplified());
    } else if (N("gear")||N("process")) { p.drawEllipse(QPointF(cx,cy),w*0.20,w*0.20);
        for(int i=0;i<8;i++){ qreal a=i*M_PI/4; dot(QPointF(cx+std::cos(a)*w*0.32,cy+std::sin(a)*w*0.32),2.2);}
    } else if (N("chart")) { qreal bw=w*0.2; qreal hs[3]={0.45,0.75,0.55};
        for(int i=0;i<3;i++){ qreal bh=h*hs[i]; p.setBrush(c); p.drawRect(QRectF(x+w*0.12+i*bw*1.25,y+h-bh,bw,bh)); } p.setBrush(Qt::NoBrush);
    } else if (N("decision")) { QPolygonF dia; dia<<QPointF(cx,y)<<QPointF(x+w,cy)<<QPointF(cx,y+h)<<QPointF(x,cy); p.drawPolygon(dia);
    } else if (N("loadbalancer")) { dot(QPointF(cx,y+h*0.16),3); for(qreal fx:{0.2,0.5,0.8}){ p.drawLine(QPointF(cx,y+h*0.16),QPointF(x+w*fx,y+h*0.84)); dot(QPointF(x+w*fx,y+h*0.84),3);}
    } else if (N("gateway")) { p.drawRoundedRect(QRectF(x+w*0.12,y+h*0.2,w*0.76,h*0.6),3,3);
        p.drawText(QRectF(x,y,w,h),Qt::AlignCenter,QStringLiteral("{ }"));
    } else if (N("queue")) { for(int i=0;i<3;i++){ qreal yy=y+h*(0.2+0.27*i);
        p.drawRect(QRectF(x+w*0.18,yy,w*0.64,h*0.2)); p.drawLine(QPointF(x+w*0.18,yy),QPointF(cx,yy+h*0.1)); p.drawLine(QPointF(cx,yy+h*0.1),QPointF(x+w*0.82,yy)); }
    } else if (N("storage")) { QPolygonF bk; bk<<QPointF(x+w*0.18,y+h*0.25)<<QPointF(x+w*0.82,y+h*0.25)<<QPointF(x+w*0.72,y+h*0.85)<<QPointF(x+w*0.28,y+h*0.85); p.drawPolygon(bk); p.drawArc(QRectF(x+w*0.18,y+h*0.16,w*0.64,h*0.18),0,180);
    } else if (N("container")) { p.drawRect(QRectF(x+w*0.12,y+h*0.3,w*0.76,h*0.55)); for(qreal fx:{0.32,0.5,0.68}) p.drawLine(QPointF(x+w*fx,y+h*0.3),QPointF(x+w*fx,y+h*0.85)); p.drawLine(QPointF(x+w*0.12,y+h*0.3),QPointF(cx,y+h*0.15)); p.drawLine(QPointF(cx,y+h*0.15),QPointF(x+w*0.88,y+h*0.3));
    } else if (N("kubernetes")) { QPolygonF hep; for(int i=0;i<7;i++){ qreal a=-M_PI/2+i*2*M_PI/7; hep<<QPointF(cx+std::cos(a)*w*0.32,cy+std::sin(a)*w*0.32);} p.drawPolygon(hep); for(int i=0;i<7;i++){ qreal a=-M_PI/2+i*2*M_PI/7; p.drawLine(QPointF(cx,cy),QPointF(cx+std::cos(a)*w*0.18,cy+std::sin(a)*w*0.18)); }
    } else if (N("function")) { p.drawText(QRectF(x,y,w,h),Qt::AlignCenter,QStringLiteral("ƒ(x)"));
    } else if (N("service")) { p.drawRoundedRect(QRectF(x+w*0.15,y+h*0.2,w*0.7,h*0.6),6,6); dot(QPointF(cx,cy),3); for(int i=0;i<4;i++){qreal a=i*M_PI/2; p.drawLine(QPointF(cx+std::cos(a)*6,cy+std::sin(a)*6),QPointF(cx+std::cos(a)*13,cy+std::sin(a)*13));}
    } else if (N("cache")) { QPolygonF z; z<<QPointF(cx+w*0.06,y+h*0.16)<<QPointF(x+w*0.34,cy+h*0.04)<<QPointF(cx,cy+h*0.04)<<QPointF(cx-w*0.06,y+h*0.84)<<QPointF(x+w*0.66,cy-h*0.04)<<QPointF(cx,cy-h*0.04); p.setBrush(c); p.drawPolygon(z); p.setBrush(Qt::NoBrush);
    } else if (N("shield")) { QPainterPath s; s.moveTo(cx,y+h*0.12); s.lineTo(x+w*0.82,y+h*0.28); s.lineTo(x+w*0.82,y+h*0.55); s.quadTo(x+w*0.82,y+h*0.8,cx,y+h*0.9); s.quadTo(x+w*0.18,y+h*0.8,x+w*0.18,y+h*0.55); s.lineTo(x+w*0.18,y+h*0.28); s.closeSubpath(); p.drawPath(s);
    } else if (N("lock")) { p.drawRoundedRect(QRectF(x+w*0.25,cy-h*0.02,w*0.5,h*0.42),3,3); QPainterPath sh; sh.moveTo(x+w*0.34,cy-h*0.02); sh.lineTo(x+w*0.34,y+h*0.28); sh.arcTo(QRectF(x+w*0.34,y+h*0.12,w*0.32,h*0.32),180,-180); sh.lineTo(x+w*0.66,cy-h*0.02); p.drawPath(sh);
    } else if (N("key")) { p.drawEllipse(QRectF(x+w*0.18,y+h*0.3,w*0.34,h*0.34)); p.drawLine(QPointF(x+w*0.5,cy),QPointF(x+w*0.86,cy)); p.drawLine(QPointF(x+w*0.74,cy),QPointF(x+w*0.74,cy+h*0.14)); p.drawLine(QPointF(x+w*0.84,cy),QPointF(x+w*0.84,cy+h*0.1));
    } else if (N("email")) { p.drawRect(QRectF(x+w*0.14,y+h*0.28,w*0.72,h*0.44)); p.drawLine(QPointF(x+w*0.14,y+h*0.28),QPointF(cx,cy+h*0.04)); p.drawLine(QPointF(cx,cy+h*0.04),QPointF(x+w*0.86,y+h*0.28));
    } else if (N("mobile")) { p.drawRoundedRect(QRectF(x+w*0.3,y+h*0.12,w*0.4,h*0.76),5,5); p.drawLine(QPointF(x+w*0.42,y+h*0.8),QPointF(x+w*0.58,y+h*0.8));
    } else if (N("desktop")) { p.drawRoundedRect(QRectF(x+w*0.12,y+h*0.2,w*0.76,h*0.46),3,3); p.drawLine(QPointF(cx,y+h*0.66),QPointF(cx,y+h*0.8)); p.drawLine(QPointF(x+w*0.34,y+h*0.84),QPointF(x+w*0.66,y+h*0.84));
    } else if (N("globe")) { p.drawEllipse(QPointF(cx,cy),w*0.3,w*0.3); p.drawEllipse(QRectF(cx-w*0.12,cy-w*0.3,w*0.24,w*0.6)); p.drawLine(QPointF(cx-w*0.3,cy),QPointF(cx+w*0.3,cy)); p.drawArc(QRectF(cx-w*0.3,cy-w*0.12,w*0.6,w*0.24),0,180);
    } else if (N("search")) { p.drawEllipse(QRectF(x+w*0.2,y+h*0.2,w*0.4,h*0.4)); p.drawLine(QPointF(x+w*0.56,y+h*0.56),QPointF(x+w*0.82,y+h*0.82));
    } else if (N("timer")) { p.drawEllipse(QPointF(cx,cy+h*0.05),w*0.3,w*0.3); p.drawLine(QPointF(cx,cy+h*0.05),QPointF(cx,cy-h*0.12)); p.drawLine(QPointF(cx,cy+h*0.05),QPointF(cx+w*0.14,cy+h*0.05)); p.drawLine(QPointF(x+w*0.4,y+h*0.08),QPointF(x+w*0.6,y+h*0.08));
    } else if (N("bell")) { QPainterPath bl; bl.moveTo(x+w*0.28,y+h*0.72); bl.lineTo(x+w*0.72,y+h*0.72); bl.quadTo(x+w*0.7,y+h*0.4,cx,y+h*0.22); bl.quadTo(x+w*0.3,y+h*0.4,x+w*0.28,y+h*0.72); p.drawPath(bl); p.drawArc(QRectF(x+w*0.42,y+h*0.74,w*0.16,h*0.12),0,-180);
    } else if (N("git")) { dot(QPointF(x+w*0.3,y+h*0.25),3); dot(QPointF(x+w*0.3,y+h*0.75),3); dot(QPointF(x+w*0.7,y+h*0.4),3); p.drawLine(QPointF(x+w*0.3,y+h*0.28),QPointF(x+w*0.3,y+h*0.72)); QPainterPath br; br.moveTo(x+w*0.3,y+h*0.6); br.quadTo(x+w*0.3,y+h*0.4,x+w*0.7,y+h*0.43); p.drawPath(br);
    } else if (N("folder")) { QPainterPath f; f.moveTo(x+w*0.15,y+h*0.3); f.lineTo(x+w*0.4,y+h*0.3); f.lineTo(x+w*0.48,y+h*0.4); f.lineTo(x+w*0.85,y+h*0.4); f.lineTo(x+w*0.85,y+h*0.78); f.lineTo(x+w*0.15,y+h*0.78); f.closeSubpath(); p.drawPath(f);
    } else if (N("browser")) { p.drawRoundedRect(QRectF(x+w*0.12,y+h*0.22,w*0.76,h*0.56),3,3); p.drawLine(QPointF(x+w*0.12,y+h*0.38),QPointF(x+w*0.88,y+h*0.38)); dot(QPointF(x+w*0.2,y+h*0.3),1.6); dot(QPointF(x+w*0.27,y+h*0.3),1.6);
    } else if (N("ai")) { p.drawEllipse(QPointF(cx,cy),w*0.26,w*0.26); dot(QPointF(cx,cy),2.5);
        for(int i=0;i<6;i++){ qreal a=i*M_PI/3; QPointF q(cx+std::cos(a)*w*0.26,cy+std::sin(a)*w*0.26); dot(q,2.2); p.drawLine(QPointF(cx,cy),q);}
    } else if (N("cpu")) { p.drawRect(QRectF(x+w*0.28,y+h*0.28,w*0.44,h*0.44)); p.drawRect(QRectF(x+w*0.38,y+h*0.38,w*0.24,h*0.24));
        for(qreal t:{0.4,0.5,0.6}){ p.drawLine(QPointF(x+w*t,y+h*0.18),QPointF(x+w*t,y+h*0.28)); p.drawLine(QPointF(x+w*t,y+h*0.72),QPointF(x+w*t,y+h*0.82)); p.drawLine(QPointF(x+w*0.18,y+h*t),QPointF(x+w*0.28,y+h*t)); p.drawLine(QPointF(x+w*0.72,y+h*t),QPointF(x+w*0.82,y+h*t)); }
    } else if (N("gpu")) { p.drawRoundedRect(QRectF(x+w*0.15,y+h*0.3,w*0.7,h*0.42),2,2); p.drawEllipse(QPointF(x+w*0.38,cy+h*0.01),w*0.1,w*0.1); p.drawEllipse(QPointF(x+w*0.62,cy+h*0.01),w*0.1,w*0.1);
    } else if (N("disk")) { p.drawEllipse(QPointF(cx,cy),w*0.3,w*0.3); p.drawEllipse(QPointF(cx,cy),w*0.08,w*0.08);
    } else if (N("backup")) { p.drawEllipse(QPointF(cx,cy),w*0.28,w*0.28); QPainterPath ar; ar.arcMoveTo(QRectF(cx-w*0.28,cy-w*0.28,w*0.56,w*0.56),60); ar.arcTo(QRectF(cx-w*0.28,cy-w*0.28,w*0.56,w*0.56),60,250); p.drawPath(ar); p.setBrush(c); QPolygonF a2; a2<<QPointF(cx+w*0.14,y+h*0.2)<<QPointF(cx+w*0.28,y+h*0.28)<<QPointF(cx+w*0.1,y+h*0.34); p.drawPolygon(a2); p.setBrush(Qt::NoBrush);
    } else if (N("sync")) { for(int s=0;s<2;s++){ qreal sgn=s?1:-1; QPainterPath ar; QRectF rr(cx-w*0.26,cy-w*0.26,w*0.52,w*0.52); ar.arcMoveTo(rr, s?20:200); ar.arcTo(rr, s?20:200,140); p.drawPath(ar); QPointF tip(cx+sgn*w*0.26*std::cos(0.35),cy+sgn*w*0.18); p.setBrush(c); p.drawEllipse(tip,2.4,2.4); p.setBrush(Qt::NoBrush);}
    } else if (N("upload")||N("download")) { bool up=N("upload"); p.drawLine(QPointF(cx,up?y+h*0.2:y+h*0.22),QPointF(cx,up?y+h*0.62:y+h*0.64));
        p.setBrush(c); QPolygonF ar; if(up) ar<<QPointF(cx,y+h*0.16)<<QPointF(cx-w*0.12,y+h*0.32)<<QPointF(cx+w*0.12,y+h*0.32); else ar<<QPointF(cx,y+h*0.68)<<QPointF(cx-w*0.12,y+h*0.52)<<QPointF(cx+w*0.12,y+h*0.52); p.drawPolygon(ar); p.setBrush(Qt::NoBrush); p.drawLine(QPointF(x+w*0.28,y+h*0.8),QPointF(x+w*0.72,y+h*0.8));
    } else if (N("code")) { p.drawPolyline(QPolygonF()<<QPointF(x+w*0.4,y+h*0.32)<<QPointF(x+w*0.24,cy)<<QPointF(x+w*0.4,y+h*0.68)); p.drawPolyline(QPolygonF()<<QPointF(x+w*0.6,y+h*0.32)<<QPointF(x+w*0.76,cy)<<QPointF(x+w*0.6,y+h*0.68));
    } else if (N("terminal")) { p.drawRoundedRect(QRectF(x+w*0.14,y+h*0.22,w*0.72,h*0.56),3,3); p.drawPolyline(QPolygonF()<<QPointF(x+w*0.26,y+h*0.4)<<QPointF(x+w*0.38,cy)<<QPointF(x+w*0.26,y+h*0.6)); p.drawLine(QPointF(x+w*0.44,y+h*0.6),QPointF(x+w*0.62,y+h*0.6));
    } else if (N("rocket")) { QPainterPath rk; rk.moveTo(cx,y+h*0.14); rk.quadTo(x+w*0.7,y+h*0.4,x+w*0.62,y+h*0.7); rk.lineTo(x+w*0.38,y+h*0.7); rk.quadTo(x+w*0.3,y+h*0.4,cx,y+h*0.14); p.drawPath(rk); dot(QPointF(cx,y+h*0.42),2.6); p.drawLine(QPointF(x+w*0.42,y+h*0.7),QPointF(x+w*0.36,y+h*0.84)); p.drawLine(QPointF(x+w*0.58,y+h*0.7),QPointF(x+w*0.64,y+h*0.84));
    } else if (N("bug")) { p.drawEllipse(QPointF(cx,cy+h*0.05),w*0.2,w*0.24); p.drawLine(QPointF(cx,cy-h*0.18),QPointF(cx,cy-h*0.05)); for(qreal s:{-1.0,1.0}){ p.drawLine(QPointF(cx,cy),QPointF(cx+s*w*0.3,cy-h*0.12)); p.drawLine(QPointF(cx,cy+h*0.1),QPointF(cx+s*w*0.3,cy+h*0.16)); }
    } else if (N("test")) { QPainterPath ck; ck.moveTo(x+w*0.3,cy); ck.lineTo(x+w*0.45,cy+h*0.16); ck.lineTo(x+w*0.72,y+h*0.3); p.setPen(QPen(c,2.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin)); p.drawPath(ck);
    } else if (N("card")) { p.drawRoundedRect(QRectF(x+w*0.14,y+h*0.3,w*0.72,h*0.42),3,3); p.setBrush(c); p.drawRect(QRectF(x+w*0.14,y+h*0.4,w*0.72,h*0.08)); p.setBrush(Qt::NoBrush); p.drawLine(QPointF(x+w*0.22,y+h*0.62),QPointF(x+w*0.4,y+h*0.62));
    } else if (N("cart")) { p.drawLine(QPointF(x+w*0.16,y+h*0.26),QPointF(x+w*0.28,y+h*0.26)); p.drawPolyline(QPolygonF()<<QPointF(x+w*0.28,y+h*0.26)<<QPointF(x+w*0.36,y+h*0.6)<<QPointF(x+w*0.74,y+h*0.6)<<QPointF(x+w*0.82,y+h*0.36)<<QPointF(x+w*0.32,y+h*0.36)); dot(QPointF(x+w*0.42,y+h*0.72),2.4); dot(QPointF(x+w*0.7,y+h*0.72),2.4);
    } else if (N("users")) { for(qreal ox:{-0.16,0.16}){ p.drawEllipse(QPointF(cx+w*ox,y+h*0.32),w*0.13,w*0.13); QPainterPath bd; bd.moveTo(cx+w*ox-w*0.16,y+h*0.78); bd.arcTo(QRectF(cx+w*ox-w*0.16,y+h*0.5,w*0.32,h*0.5),180,-180); p.drawPath(bd);}
    } else if (N("bank")) { QPolygonF rf; rf<<QPointF(x+w*0.14,y+h*0.34)<<QPointF(cx,y+h*0.18)<<QPointF(x+w*0.86,y+h*0.34); p.drawPolyline(rf); for(qreal t:{0.26,0.5,0.74}) p.drawLine(QPointF(x+w*t,y+h*0.4),QPointF(x+w*t,y+h*0.74)); p.drawLine(QPointF(x+w*0.14,y+h*0.8),QPointF(x+w*0.86,y+h*0.8));
    } else if (N("truck")) { p.drawRect(QRectF(x+w*0.12,y+h*0.34,w*0.42,h*0.34)); QPolygonF cab; cab<<QPointF(x+w*0.54,y+h*0.44)<<QPointF(x+w*0.72,y+h*0.44)<<QPointF(x+w*0.84,y+h*0.58)<<QPointF(x+w*0.84,y+h*0.68)<<QPointF(x+w*0.54,y+h*0.68); p.drawPolygon(cab); dot(QPointF(x+w*0.26,y+h*0.74),3); dot(QPointF(x+w*0.7,y+h*0.74),3);
    } else if (N("package")) { p.drawRect(QRectF(x+w*0.2,y+h*0.3,w*0.6,h*0.5)); p.drawLine(QPointF(x+w*0.2,y+h*0.46),QPointF(x+w*0.8,y+h*0.46)); p.drawLine(QPointF(cx,y+h*0.46),QPointF(cx,y+h*0.8));
    } else if (N("factory")) { QPolygonF ff; ff<<QPointF(x+w*0.16,y+h*0.78)<<QPointF(x+w*0.16,y+h*0.48)<<QPointF(x+w*0.42,y+h*0.62)<<QPointF(x+w*0.42,y+h*0.48)<<QPointF(x+w*0.7,y+h*0.62)<<QPointF(x+w*0.7,y+h*0.36)<<QPointF(x+w*0.84,y+h*0.36)<<QPointF(x+w*0.84,y+h*0.78); p.drawPolygon(ff);
    } else if (N("robot")) { p.drawRoundedRect(QRectF(x+w*0.24,y+h*0.32,w*0.52,h*0.4),5,5); dot(QPointF(x+w*0.4,cy+h*0.02),2.6); dot(QPointF(x+w*0.6,cy+h*0.02),2.6); p.drawLine(QPointF(cx,y+h*0.2),QPointF(cx,y+h*0.32)); dot(QPointF(cx,y+h*0.18),2.2);
    } else if (N("chat")) { QPainterPath ch; ch.addRoundedRect(QRectF(x+w*0.16,y+h*0.24,w*0.68,h*0.42),6,6); p.drawPath(ch); QPolygonF tl; tl<<QPointF(x+w*0.3,y+h*0.66)<<QPointF(x+w*0.3,y+h*0.8)<<QPointF(x+w*0.44,y+h*0.66); p.drawPolygon(tl);
    } else if (N("wifi")) { for(qreal r:{0.12,0.22,0.32}){ p.drawArc(QRectF(cx-w*r,y+h*0.3,w*2*r,w*2*r),20,140);} dot(QPointF(cx,y+h*0.66),2.6);
    } else if (N("camera")) { p.drawRoundedRect(QRectF(x+w*0.14,y+h*0.34,w*0.72,h*0.4),3,3); p.drawRect(QRectF(x+w*0.36,y+h*0.28,w*0.18,h*0.08)); p.drawEllipse(QPointF(cx,cy+h*0.07),w*0.12,w*0.12);
    } else if (N("calendar")) { p.drawRoundedRect(QRectF(x+w*0.16,y+h*0.24,w*0.68,h*0.56),3,3); p.drawLine(QPointF(x+w*0.16,y+h*0.4),QPointF(x+w*0.84,y+h*0.4)); p.drawLine(QPointF(x+w*0.34,y+h*0.18),QPointF(x+w*0.34,y+h*0.3)); p.drawLine(QPointF(x+w*0.66,y+h*0.18),QPointF(x+w*0.66,y+h*0.3));
    } else if (N("pin")) { QPainterPath pn; pn.moveTo(cx,y+h*0.84); pn.quadTo(x+w*0.22,cy,cx,y+h*0.2); pn.quadTo(x+w*0.78,cy,cx,y+h*0.84); p.drawPath(pn); p.drawEllipse(QPointF(cx,y+h*0.4),w*0.1,w*0.1);
    } else if (N("tag")) { QPainterPath tg; tg.moveTo(x+w*0.2,y+h*0.3); tg.lineTo(x+w*0.55,y+h*0.3); tg.lineTo(x+w*0.8,cy+h*0.05); tg.lineTo(x+w*0.55,y+h*0.8); tg.lineTo(x+w*0.2,y+h*0.8); tg.closeSubpath(); p.drawPath(tg); dot(QPointF(x+w*0.32,cy+h*0.05),2.4);
    } else if (N("star")) { QPolygonF st; for(int i=0;i<10;i++){ qreal a=-M_PI/2+i*M_PI/5; qreal r=(i%2)?w*0.12:w*0.3; st<<QPointF(cx+std::cos(a)*r,cy+std::sin(a)*r);} p.drawPolygon(st);
    } else if (N("warning")) { QPolygonF tr; tr<<QPointF(cx,y+h*0.2)<<QPointF(x+w*0.82,y+h*0.78)<<QPointF(x+w*0.18,y+h*0.78); p.drawPolygon(tr); p.drawLine(QPointF(cx,y+h*0.4),QPointF(cx,y+h*0.6)); dot(QPointF(cx,y+h*0.7),1.8);
    } else if (N("info")) { p.drawEllipse(QPointF(cx,cy),w*0.3,w*0.3); p.drawLine(QPointF(cx,cy-h*0.02),QPointF(cx,cy+h*0.14)); dot(QPointF(cx,cy-h*0.14),1.8);
    } else if (N("link")) { p.drawRoundedRect(QRectF(cx-w*0.30,cy-h*0.14,w*0.34,h*0.28),9,9); p.drawRoundedRect(QRectF(cx-w*0.02,cy-h*0.14,w*0.34,h*0.28),9,9); p.drawLine(QPointF(cx-w*0.04,cy),QPointF(cx+w*0.06,cy));
    } else if (N("filter")) { QPolygonF fn; fn<<QPointF(x+w*0.18,y+h*0.26)<<QPointF(x+w*0.82,y+h*0.26)<<QPointF(x+w*0.58,cy+h*0.02)<<QPointF(x+w*0.58,y+h*0.78)<<QPointF(x+w*0.42,y+h*0.66)<<QPointF(x+w*0.42,cy+h*0.02); p.drawPolygon(fn);
    } else { p.drawRoundedRect(b,4,4); }
    p.restore();
}

Layout computeLayout(const Npd::Diagram &d) {
    Layout lay;
    QStringList ids; QHash<QString,Npd::Node> byId;
    for (const auto&n:d.nodes){ ids<<n.id; byId[n.id]=n; }

    QHash<QString,int> layer; for(const auto&id:ids) layer[id]=0;
    for(int it=0; it<ids.size()+2; ++it){ bool ch=false;
        for(const auto&e:d.edges){ int w=layer.value(e.from,0)+1;
            if(w>layer.value(e.to,0) && w<ids.size()+1){ layer[e.to]=w; ch=true; } } if(!ch) break; }

    QMap<int,QStringList> rows; for(const auto&id:ids) rows[layer.value(id,0)]<<id;

    QHash<QString,QVector<QString>> succ, pred;
    for(const auto&e:d.edges){ succ[e.from]<<e.to; pred[e.to]<<e.from; }
    QHash<QString,int> pos; auto reindex=[&](){ for(auto&v:rows){ for(int i=0;i<v.size();++i) pos[v[i]]=i; } };
    reindex();
    for(int sweep=0; sweep<10; ++sweep){
        QList<int> ls=rows.keys(); if(sweep%2) std::reverse(ls.begin(),ls.end());
        for(int L:ls){ auto&row=rows[L];
            QHash<QString,double> bc;
            for(const auto&id:row){ const auto&nb=(sweep%2)?succ.value(id):pred.value(id);
                if(nb.isEmpty()){ bc[id]=pos.value(id); continue; }
                double s=0; for(const auto&m:nb) s+=pos.value(m,0); bc[id]=s/nb.size(); }
            std::stable_sort(row.begin(),row.end(),[&](const QString&a,const QString&b){return bc[a]<bc[b];});
            reindex();
        }
    }

    QFontMetrics fm(uiFont(15,true));
    const qreal hGap=78, vGap=136, margin=70;
    qreal yTop=margin+(d.title.isEmpty()?0:60);
    qreal widest=0; QMap<int,qreal> rowW;
    for(auto it=rows.begin();it!=rows.end();++it){ qreal tw=0; for(const auto&id:it.value()) tw+=sizeFor(byId[id],fm).width(); tw+=hGap*(it.value().size()-1); rowW[it.key()]=tw; widest=std::max(widest,tw); }
    qreal canvasW=widest+2*margin;
    if(!d.title.isEmpty()){ QFontMetrics fmT(uiFont(23,true)); canvasW=std::max(canvasW, fmT.horizontalAdvance(d.title)+2.0*margin); }
    if(!d.textboxes.isEmpty()){ QFontMetrics fmC(uiFont(13)); canvasW=std::max(canvasW, fmC.horizontalAdvance(d.textboxes.join("   •   "))+2.0*margin+24); }
    qreal y=yTop;
    for(auto it=rows.begin();it!=rows.end();++it){ qreal x=(canvasW-rowW[it.key()])/2.0; qreal rowH=0;
        for(const auto&id:it.value()){ QSizeF s=sizeFor(byId[id],fm); lay.nodeRects[id]=QRectF(x,y,s.width(),s.height()); x+=s.width()+hGap; rowH=std::max(rowH,s.height()); }
        y+=rowH+vGap;
    }
    lay.canvasW=canvasW;
    lay.canvasH=y-vGap+margin+(d.textboxes.isEmpty()?0:46);
    lay.layer=layer;
    return lay;
}

void paint(QPainter &p, const Npd::Diagram &d, const Layout &lay, const Palette &pal) {
    const qreal margin=70, canvasW=lay.canvasW, canvasH=lay.canvasH;
    const QHash<QString,QRectF> &geo = lay.nodeRects;
    p.setRenderHint(QPainter::Antialiasing); p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(QRectF(0,0,canvasW,canvasH), pal.bg);

    if(!d.title.isEmpty()){ p.setFont(uiFont(23,true)); p.setPen(pal.title);
        p.drawText(QRectF(margin,20,canvasW-2*margin,42),Qt::AlignLeft|Qt::AlignVCenter,d.title); }

    QHash<QString,QList<int>> downOut, topIn, upOut, botIn;
    for(int i=0;i<d.edges.size();++i){ const auto&e=d.edges[i];
        if(!geo.contains(e.from)||!geo.contains(e.to)) continue;
        bool down = geo[e.to].center().y() >= geo[e.from].center().y();
        if(down){ downOut[e.from]<<i; topIn[e.to]<<i; } else { upOut[e.from]<<i; botIn[e.to]<<i; } }
    QHash<int,QPointF> sa, da;
    auto spread=[&](QHash<QString,QList<int>>&grp, bool atBottom, bool keyIsSource, QHash<int,QPointF>&out){
        for(auto it=grp.begin();it!=grp.end();++it){ QList<int> g=it.value(); QRectF r=geo[it.key()];
            std::sort(g.begin(),g.end(),[&](int x,int y){
                qreal ax=(keyIsSource?geo[d.edges[x].to]:geo[d.edges[x].from]).center().x();
                qreal ay=(keyIsSource?geo[d.edges[y].to]:geo[d.edges[y].from]).center().x(); return ax<ay; });
            int m=g.size(); for(int k=0;k<m;++k){
                qreal x=r.left()+r.width()*(k+1.0)/(m+1.0);
                out[g[k]]=QPointF(x, atBottom?r.bottom():r.top()); } } };
    spread(downOut,true,true,sa);  spread(topIn,false,false,da);
    spread(upOut,false,true,sa);   spread(botIn,true,false,da);

    p.setFont(uiFont(12));
    for(int i=0;i<d.edges.size();++i){ const auto&e=d.edges[i];
        if(!geo.contains(e.from)||!geo.contains(e.to)) continue;
        QRectF a=geo[e.from], b=geo[e.to];
        QPointF p1=sa.value(i, borderPoint(a,b.center()));
        QPointF p2=da.value(i, borderPoint(b,a.center()));
        bool vert = std::abs(p2.y()-p1.y()) >= std::abs(p2.x()-p1.x());
        int span = std::abs(lay.layer.value(e.to,0)-lay.layer.value(e.from,0));
        QPointF c1,c2;
        if(vert){ qreal dy=(p2.y()-p1.y())*0.42;
            qreal mx=(p1.x()+p2.x())/2.0; qreal side = mx<=canvasW/2 ? -1 : 1;
            qreal bow=(span>1)? (105.0+45.0*(span-1)) * side : 0.0;
            c1={p1.x()+bow,p1.y()+dy}; c2={p2.x()+bow,p2.y()-dy}; }
        else    { qreal dx=(p2.x()-p1.x())*0.45; c1={p1.x()+dx,p1.y()}; c2={p2.x()-dx,p2.y()}; }
        QPainterPath path(p1); path.cubicTo(c1,c2,p2);
        p.setPen(QPen(pal.edge,2,Qt::SolidLine,Qt::RoundCap)); p.setBrush(Qt::NoBrush); p.drawPath(path);
        auto head=[&](QPointF tip,QPointF ctrl){ QLineF u(ctrl,tip); qreal ang=std::atan2(u.dy(),u.dx());
            const qreal L=13,s=0.42; QPointF h1(tip.x()-L*std::cos(ang-s),tip.y()-L*std::sin(ang-s));
            QPointF h2(tip.x()-L*std::cos(ang+s),tip.y()-L*std::sin(ang+s));
            QPolygonF t; t<<tip<<h1<<h2; p.setBrush(pal.edge); p.setPen(Qt::NoPen); p.drawPolygon(t); p.setBrush(Qt::NoBrush); };
        head(p2,c2); if(e.bidirectional) head(p1,c1);
        if(!e.label.isEmpty()){ QPointF mid=path.pointAtPercent(0.5);
            QRectF lr=p.fontMetrics().boundingRect(e.label).adjusted(-7,-3,7,3); lr.moveCenter(mid);
            p.setBrush(pal.bg); p.setPen(QPen(pal.border,1)); p.drawRoundedRect(lr,5,5);
            p.setPen(pal.sub); p.drawText(lr,Qt::AlignCenter,e.label); }
    }

    for(const auto&n:d.nodes){ if(!geo.contains(n.id)) continue; QRectF r=geo[n.id];
        p.setPen(Qt::NoPen); p.setBrush(QColor(0,0,0,70));
        if(n.shape==Npd::Shape::Pill) p.drawRoundedRect(r.translated(0,3),r.height()/2,r.height()/2);
        else if(n.shape!=Npd::Shape::Decision) p.drawRoundedRect(r.translated(0,3),10,10);

        QLinearGradient g(r.topLeft(),r.bottomLeft()); g.setColorAt(0,pal.card); g.setColorAt(1,pal.card2);
        p.setBrush(g); p.setPen(QPen(pal.border,2));
        switch(n.shape){
        case Npd::Shape::Pill: p.drawRoundedRect(r,r.height()/2,r.height()/2); break;
        case Npd::Shape::Decision:{ QPolygonF dia; dia<<QPointF(r.center().x(),r.top())<<QPointF(r.right(),r.center().y())
            <<QPointF(r.center().x(),r.bottom())<<QPointF(r.left(),r.center().y()); p.drawPolygon(dia); break; }
        case Npd::Shape::Database:{ qreal e=15;
            p.drawEllipse(QRectF(r.left(),r.bottom()-e,r.width(),e));
            p.setPen(Qt::NoPen); p.drawRect(QRectF(r.left(),r.top()+e/2,r.width(),r.height()-e));
            p.setPen(QPen(pal.border,2));
            p.drawLine(QPointF(r.left(),r.top()+e/2),QPointF(r.left(),r.bottom()-e/2));
            p.drawLine(QPointF(r.right(),r.top()+e/2),QPointF(r.right(),r.bottom()-e/2));
            p.setBrush(g); p.drawEllipse(QRectF(r.left(),r.top(),r.width(),e));
            break; }
        case Npd::Shape::Icon: p.drawRoundedRect(r,12,12); break;
        default: p.drawRoundedRect(r,10,10); break;
        }
        p.setPen(pal.text);
        if(n.shape==Npd::Shape::Icon){
            QString ic=n.icon.isEmpty()?QStringLiteral("process"):n.icon;
            drawIcon(p,ic,QRectF(r.center().x()-17,r.top()+12,34,34),pal.accent);
            p.setFont(uiFont(13,true)); p.setPen(pal.text);
            p.drawText(QRectF(r.left()+4,r.bottom()-30,r.width()-8,26),Qt::AlignCenter,n.label.isEmpty()?n.id:n.label);
        } else {
            p.setFont(uiFont(15,true));
            p.drawText(r.adjusted(8,0,-8,0),Qt::AlignCenter|Qt::TextWordWrap,n.label.isEmpty()?n.id:n.label);
        }
        if(!n.hover.isEmpty()){ p.setBrush(pal.accent); p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(r.right()-8,r.top()+8),3.2,3.2); }
    }

    if(!d.textboxes.isEmpty()){ p.setFont(uiFont(13)); p.setPen(pal.sub);
        p.drawText(QRectF(margin,canvasH-margin-22,canvasW-2*margin,26),Qt::AlignLeft,
                   QString::fromUtf8("“")+d.textboxes.join("   •   ")+QString::fromUtf8("”")); }
}

QString nodeAt(const Npd::Diagram &d, const Layout &lay, QPointF pt) {
    // reverse order so topmost (later-drawn) wins
    for(int i=d.nodes.size()-1;i>=0;--i){ const auto&n=d.nodes[i];
        auto it=lay.nodeRects.find(n.id);
        if(it!=lay.nodeRects.end() && it.value().contains(pt)) return n.id; }
    return QString();
}

} // namespace DiagramRender
