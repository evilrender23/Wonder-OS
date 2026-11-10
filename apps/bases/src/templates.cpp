#include "templates.h"

#include "builder.h"
#include "database.h"
#include "sqlutil.h"

#include <QDate>

namespace Templates
{

QVariantList list()
{
    return {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("contactos")}, {QStringLiteral("name"), QStringLiteral("Contactos")},
                    {QStringLiteral("description"), QStringLiteral("Personas, empresas y las notas de cada conversación.")}, {QStringLiteral("symbol"), QStringLiteral("form")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("inventario")}, {QStringLiteral("name"), QStringLiteral("Inventario")},
                    {QStringLiteral("description"), QStringLiteral("Artículos, categorías, ubicaciones y movimientos de existencias.")}, {QStringLiteral("symbol"), QStringLiteral("rows-3")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("tareas")}, {QStringLiteral("name"), QStringLiteral("Tareas")},
                    {QStringLiteral("description"), QStringLiteral("Proyectos y tareas con prioridad, estado y fecha límite.")}, {QStringLiteral("symbol"), QStringLiteral("square-check")}},
    };
}

static bool runAll(Database &db, const QStringList &statements, QString *error)
{
    if (!db.begin()) {
        if (error) {
            *error = db.lastError();
        }
        return false;
    }
    for (const QString &statement : statements) {
        const Result result = db.query(statement);
        if (!result.ok) {
            db.rollback();
            if (error) {
                *error = result.error;
            }
            return false;
        }
    }
    if (!db.commit()) {
        if (error) {
            *error = db.lastError();
        }
        db.rollback();
        return false;
    }
    db.touch(true);
    return true;
}

static void addForm(Database &db, const QString &name, const QString &table)
{
    db.saveObject(QStringLiteral("form"), name, Builder::defaultForm(db, table));
}

bool apply(Database &db, const QString &id, QString *error)
{
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    if (id == QLatin1String("contactos")) {
        if (!runAll(db, {
                QStringLiteral("CREATE TABLE \"empresas\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"nombre\" TEXT NOT NULL, \"sector\" TEXT, \"web\" TEXT, \"ciudad\" TEXT)"),
                QStringLiteral("CREATE TABLE \"contactos\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"nombre\" TEXT NOT NULL, \"apellidos\" TEXT, \"empresa_id\" INTEGER REFERENCES \"empresas\" (\"id\") ON DELETE SET NULL, "
                               "\"cargo\" TEXT, \"email\" TEXT, \"telefono\" TEXT, \"cumpleanos\" DATE, \"favorito\" BOOLEAN NOT NULL DEFAULT 0, \"notas\" TEXT)"),
                QStringLiteral("CREATE TABLE \"conversaciones\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"contacto_id\" INTEGER NOT NULL REFERENCES \"contactos\" (\"id\") ON DELETE CASCADE, "
                               "\"fecha\" DATE NOT NULL DEFAULT CURRENT_DATE, \"medio\" TEXT, \"resumen\" TEXT)"),
                QStringLiteral("CREATE INDEX \"idx_contactos_apellidos\" ON \"contactos\" (\"apellidos\")"),
            }, error)) {
            return false;
        }
        addForm(db, QStringLiteral("Contactos"), QStringLiteral("contactos"));
        db.saveObject(QStringLiteral("query"), QStringLiteral("Favoritos"), {{QStringLiteral("sql"), QStringLiteral("SELECT nombre, apellidos, email, telefono\nFROM contactos\nWHERE favorito = 1\nORDER BY apellidos")}});
        return true;
    }
    if (id == QLatin1String("inventario")) {
        if (!runAll(db, {
                QStringLiteral("CREATE TABLE \"categorias\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"nombre\" TEXT NOT NULL UNIQUE)"),
                QStringLiteral("CREATE TABLE \"ubicaciones\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"nombre\" TEXT NOT NULL UNIQUE, \"descripcion\" TEXT)"),
                QStringLiteral("CREATE TABLE \"articulos\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"codigo\" TEXT UNIQUE, \"nombre\" TEXT NOT NULL, "
                               "\"categoria_id\" INTEGER REFERENCES \"categorias\" (\"id\") ON DELETE SET NULL, \"ubicacion_id\" INTEGER REFERENCES \"ubicaciones\" (\"id\") ON DELETE SET NULL, "
                               "\"existencias\" INTEGER NOT NULL DEFAULT 0, \"minimo\" INTEGER NOT NULL DEFAULT 0, \"precio\" REAL, \"descatalogado\" BOOLEAN NOT NULL DEFAULT 0)"),
                QStringLiteral("CREATE TABLE \"movimientos\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"articulo_id\" INTEGER NOT NULL REFERENCES \"articulos\" (\"id\") ON DELETE CASCADE, "
                               "\"fecha\" DATE NOT NULL DEFAULT CURRENT_DATE, \"cantidad\" INTEGER NOT NULL, \"motivo\" TEXT)"),
                QStringLiteral("INSERT INTO \"categorias\" (\"nombre\") VALUES ('General')"),
                QStringLiteral("INSERT INTO \"ubicaciones\" (\"nombre\") VALUES ('Almacén')"),
            }, error)) {
            return false;
        }
        addForm(db, QStringLiteral("Artículos"), QStringLiteral("articulos"));
        db.saveObject(QStringLiteral("query"), QStringLiteral("Bajo mínimos"), {{QStringLiteral("sql"), QStringLiteral("SELECT codigo, nombre, existencias, minimo\nFROM articulos\nWHERE existencias < minimo AND descatalogado = 0\nORDER BY nombre")}});
        return true;
    }
    if (id == QLatin1String("tareas")) {
        if (!runAll(db, {
                QStringLiteral("CREATE TABLE \"proyectos\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"nombre\" TEXT NOT NULL, \"inicio\" DATE, \"cerrado\" BOOLEAN NOT NULL DEFAULT 0)"),
                QStringLiteral("CREATE TABLE \"tareas\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"titulo\" TEXT NOT NULL, \"proyecto_id\" INTEGER REFERENCES \"proyectos\" (\"id\") ON DELETE CASCADE, "
                               "\"prioridad\" INTEGER NOT NULL DEFAULT 2, \"estado\" TEXT NOT NULL DEFAULT 'pendiente', \"vence\" DATE, \"hecha\" BOOLEAN NOT NULL DEFAULT 0, \"notas\" TEXT)"),
                QStringLiteral("CREATE INDEX \"idx_tareas_vence\" ON \"tareas\" (\"vence\")"),
                QStringLiteral("INSERT INTO \"proyectos\" (\"nombre\", \"inicio\") VALUES ('Personal', %1)").arg(Sql::literal(today)),
            }, error)) {
            return false;
        }
        addForm(db, QStringLiteral("Tareas"), QStringLiteral("tareas"));
        db.saveObject(QStringLiteral("query"), QStringLiteral("Pendientes"), {{QStringLiteral("sql"), QStringLiteral("SELECT titulo, prioridad, vence\nFROM tareas\nWHERE hecha = 0\nORDER BY vence, prioridad")}});
        return true;
    }
    if (error) {
        *error = QStringLiteral("No existe la plantilla «%1».").arg(id);
    }
    return false;
}

bool sample(Database &db, QString *error)
{
    static const QStringList names = {QStringLiteral("Ana"), QStringLiteral("Luis"), QStringLiteral("Marta"), QStringLiteral("Jorge"), QStringLiteral("Lucía"), QStringLiteral("Pablo"),
                                      QStringLiteral("Elena"), QStringLiteral("Diego"), QStringLiteral("Carmen"), QStringLiteral("Hugo"), QStringLiteral("Sara"), QStringLiteral("Iván"),
                                      QStringLiteral("Nuria"), QStringLiteral("Óscar"), QStringLiteral("Irene"), QStringLiteral("Raúl")};
    static const QStringList surnames = {QStringLiteral("García"), QStringLiteral("Martínez"), QStringLiteral("López"), QStringLiteral("Sánchez"), QStringLiteral("Pérez"),
                                         QStringLiteral("Gómez"), QStringLiteral("Ruiz"), QStringLiteral("Díaz"), QStringLiteral("Moreno"), QStringLiteral("Muñoz"),
                                         QStringLiteral("Álvarez"), QStringLiteral("Romero"), QStringLiteral("Navarro"), QStringLiteral("Torres"), QStringLiteral("Vega")};
    static const QStringList cities = {QStringLiteral("Madrid"), QStringLiteral("Barcelona"), QStringLiteral("Valencia"), QStringLiteral("Sevilla"), QStringLiteral("Bilbao"),
                                       QStringLiteral("Zaragoza"), QStringLiteral("Málaga"), QStringLiteral("A Coruña")};
    static const QList<QPair<QString, QStringList>> catalogue = {
        {QStringLiteral("Papelería"), {QStringLiteral("Cuaderno A5"), QStringLiteral("Cuaderno A4"), QStringLiteral("Bolígrafo azul"), QStringLiteral("Bolígrafo negro"), QStringLiteral("Lápiz HB"),
                                       QStringLiteral("Rotulador fino"), QStringLiteral("Bloc de notas"), QStringLiteral("Agenda semanal"), QStringLiteral("Carpeta de anillas"), QStringLiteral("Sobres (50 u.)")}},
        {QStringLiteral("Escritorio"), {QStringLiteral("Lámpara de mesa"), QStringLiteral("Alfombrilla"), QStringLiteral("Soporte de portátil"), QStringLiteral("Organizador"), QStringLiteral("Reposapiés"),
                                        QStringLiteral("Bandeja apilable"), QStringLiteral("Reloj de mesa"), QStringLiteral("Pizarra blanca"), QStringLiteral("Corcho mural"), QStringLiteral("Papelera")}},
        {QStringLiteral("Informática"), {QStringLiteral("Teclado"), QStringLiteral("Ratón"), QStringLiteral("Cable USB-C"), QStringLiteral("Memoria USB 64 GB"), QStringLiteral("Auriculares"),
                                         QStringLiteral("Cámara web"), QStringLiteral("Concentrador USB"), QStringLiteral("Funda de portátil"), QStringLiteral("Adaptador HDMI"), QStringLiteral("Regleta")}},
        {QStringLiteral("Café"), {QStringLiteral("Café molido 250 g"), QStringLiteral("Café en grano 1 kg"), QStringLiteral("Té verde"), QStringLiteral("Taza de cerámica"), QStringLiteral("Termo 500 ml"),
                                  QStringLiteral("Filtros (100 u.)"), QStringLiteral("Azúcar moreno"), QStringLiteral("Galletas"), QStringLiteral("Cacao"), QStringLiteral("Infusión de menta")}},
    };
    static const QStringList states = {QStringLiteral("entregado"), QStringLiteral("entregado"), QStringLiteral("entregado"), QStringLiteral("enviado"), QStringLiteral("pendiente"), QStringLiteral("cancelado")};

    // Fixed numbers: the pictures of the tests come out the same every run.
    quint32 seed = 20261110;
    const auto next = [&seed](int below) {
        seed = seed * 1664525u + 1013904223u;
        return int((seed >> 8) % quint32(below));
    };

    if (!db.begin()) {
        if (error) {
            *error = db.lastError();
        }
        return false;
    }
    const auto run = [&](const QString &sql, const QVariantList &binds = {}) {
        const Result result = db.query(sql, binds);
        if (!result.ok && error && error->isEmpty()) {
            *error = result.error;
        }
        return result.ok;
    };
    QString scratch;
    if (!error) {
        error = &scratch;
    }
    error->clear();
    bool ok = run(QStringLiteral("CREATE TABLE \"clientes\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"nombre\" TEXT NOT NULL, \"email\" TEXT, \"ciudad\" TEXT, \"alta\" DATE, \"activo\" BOOLEAN NOT NULL DEFAULT 1)"))
        && run(QStringLiteral("CREATE TABLE \"productos\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"nombre\" TEXT NOT NULL, \"categoria\" TEXT, \"precio\" REAL NOT NULL DEFAULT 0, \"existencias\" INTEGER NOT NULL DEFAULT 0)"))
        && run(QStringLiteral("CREATE TABLE \"pedidos\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"cliente_id\" INTEGER NOT NULL REFERENCES \"clientes\" (\"id\") ON DELETE CASCADE, "
                              "\"fecha\" DATE NOT NULL, \"estado\" TEXT NOT NULL DEFAULT 'pendiente', \"total\" REAL NOT NULL DEFAULT 0)"))
        && run(QStringLiteral("CREATE TABLE \"lineas_pedido\" (\"id\" INTEGER PRIMARY KEY AUTOINCREMENT, \"pedido_id\" INTEGER NOT NULL REFERENCES \"pedidos\" (\"id\") ON DELETE CASCADE, "
                              "\"producto_id\" INTEGER NOT NULL REFERENCES \"productos\" (\"id\"), \"cantidad\" INTEGER NOT NULL DEFAULT 1, \"precio\" REAL NOT NULL)"))
        && run(QStringLiteral("CREATE INDEX \"idx_pedidos_cliente\" ON \"pedidos\" (\"cliente_id\")"))
        && run(QStringLiteral("CREATE INDEX \"idx_lineas_pedido\" ON \"lineas_pedido\" (\"pedido_id\")"));

    const QDate start(2025, 1, 6);
    constexpr int customers = 60;
    for (int i = 0; ok && i < customers; ++i) {
        const QString name = names.at(next(int(names.size())));
        const QString surname = surnames.at(next(int(surnames.size())));
        QString mail = (name + QLatin1Char('.') + surname).toLower().normalized(QString::NormalizationForm_D);
        QString ascii;
        for (const QChar c : std::as_const(mail)) {
            if (c.unicode() < 128) {
                ascii += c;
            }
        }
        ok = run(QStringLiteral("INSERT INTO \"clientes\" (\"nombre\", \"email\", \"ciudad\", \"alta\", \"activo\") VALUES (?, ?, ?, ?, ?)"),
                 {name + QLatin1Char(' ') + surname, i % 11 == 7 ? QVariant() : QVariant(QStringLiteral("%1%2@ejemplo.es").arg(ascii).arg(i + 1)), cities.at(next(int(cities.size()))),
                  start.addDays(next(420)).toString(Qt::ISODate), i % 9 == 4 ? 0 : 1});
    }
    QList<double> prices;
    for (const auto &[category, items] : catalogue) {
        for (const QString &item : items) {
            const double price = (150 + next(5800)) / 100.0;
            prices.append(price);
            ok = ok && run(QStringLiteral("INSERT INTO \"productos\" (\"nombre\", \"categoria\", \"precio\", \"existencias\") VALUES (?, ?, ?, ?)"), {item, category, price, next(240)});
        }
    }
    constexpr int orders = 240;
    for (int i = 0; ok && i < orders; ++i) {
        ok = run(QStringLiteral("INSERT INTO \"pedidos\" (\"cliente_id\", \"fecha\", \"estado\") VALUES (?, ?, ?)"),
                 {1 + next(customers), start.addDays(30 + next(640)).toString(Qt::ISODate), states.at(next(int(states.size())))});
        const int lines = 1 + next(4);
        for (int line = 0; ok && line < lines; ++line) {
            const int product = next(int(prices.size()));
            ok = run(QStringLiteral("INSERT INTO \"lineas_pedido\" (\"pedido_id\", \"producto_id\", \"cantidad\", \"precio\") VALUES (?, ?, ?, ?)"), {i + 1, product + 1, 1 + next(6), prices.at(product)});
        }
    }
    ok = ok && run(QStringLiteral("UPDATE \"pedidos\" SET \"total\" = (SELECT round(sum(\"cantidad\" * \"precio\"), 2) FROM \"lineas_pedido\" WHERE \"pedido_id\" = \"pedidos\".\"id\")"));
    ok = ok && run(QStringLiteral("CREATE VIEW \"ventas_por_ciudad\" AS SELECT c.\"ciudad\" AS ciudad, count(p.\"id\") AS pedidos, round(sum(p.\"total\"), 2) AS importe "
                                  "FROM \"clientes\" c JOIN \"pedidos\" p ON p.\"cliente_id\" = c.\"id\" WHERE p.\"estado\" <> 'cancelado' GROUP BY c.\"ciudad\""));
    if (!ok || !db.commit()) {
        if (error->isEmpty()) {
            *error = db.lastError();
        }
        db.rollback();
        return false;
    }
    db.touch(true);

    db.saveObject(QStringLiteral("query"), QStringLiteral("Pedidos pendientes"),
                  {{QStringLiteral("sql"), QStringLiteral("SELECT p.id, c.nombre AS cliente, p.fecha, p.total\nFROM pedidos p\nJOIN clientes c ON c.id = p.cliente_id\nWHERE p.estado = 'pendiente'\nORDER BY p.fecha DESC")}});
    db.saveObject(QStringLiteral("query"), QStringLiteral("Pedidos de una ciudad"),
                  {{QStringLiteral("sql"), QStringLiteral("SELECT c.nombre, p.fecha, p.estado, p.total\nFROM pedidos p\nJOIN clientes c ON c.id = p.cliente_id\nWHERE c.ciudad = :ciudad\nORDER BY p.fecha")}});
    QVariantMap design{
        {QStringLiteral("tables"), QStringList{QStringLiteral("productos"), QStringLiteral("lineas_pedido")}},
        {QStringLiteral("columns"), QVariantList{
            QVariantMap{{QStringLiteral("table"), QStringLiteral("productos")}, {QStringLiteral("column"), QStringLiteral("categoria")}, {QStringLiteral("show"), true}, {QStringLiteral("total"), QStringLiteral("group")}, {QStringLiteral("sort"), QStringLiteral("asc")}},
            QVariantMap{{QStringLiteral("table"), QStringLiteral("lineas_pedido")}, {QStringLiteral("column"), QStringLiteral("cantidad")}, {QStringLiteral("show"), true}, {QStringLiteral("total"), QStringLiteral("sum")}, {QStringLiteral("alias"), QStringLiteral("unidades")}},
        }},
    };
    db.saveObject(QStringLiteral("query"), QStringLiteral("Unidades por categoría"), {{QStringLiteral("design"), design}, {QStringLiteral("sql"), Builder::sql(db, design)}});
    db.saveObject(QStringLiteral("form"), QStringLiteral("Clientes"), Builder::defaultForm(db, QStringLiteral("clientes")));
    QVariantMap report = Builder::defaultReport(db, QStringLiteral("productos"));
    report.insert(QStringLiteral("title"), QStringLiteral("Catálogo de productos"));
    report.insert(QStringLiteral("groupBy"), QStringLiteral("categoria"));
    QVariantList columns;
    for (const QVariant &item : report.value(QStringLiteral("columns")).toList()) {
        QVariantMap column = item.toMap();
        const QString name = column.value(QStringLiteral("name")).toString();
        if (name == QLatin1String("id") || name == QLatin1String("categoria")) {
            column.insert(QStringLiteral("show"), false);
        }
        column.insert(QStringLiteral("total"), name == QLatin1String("existencias") ? QStringLiteral("sum") : name == QLatin1String("precio") ? QStringLiteral("avg") : QString());
        columns.append(column);
    }
    report.insert(QStringLiteral("columns"), columns);
    db.saveObject(QStringLiteral("report"), QStringLiteral("Catálogo de productos"), report);
    return true;
}

}
