#include "TestFramework.hpp"
#include "db/Database.hpp"
#include "db/Migrations.hpp"

using namespace eden;

EDEN_TEST(database_migrates_to_latest_version) {
    Database db = Database::openMemory();
    EXPECT_EQ(migrate(db), latestSchemaVersion());
    EXPECT_EQ(db.userVersion(), static_cast<std::int64_t>(latestSchemaVersion()));
    EXPECT_TRUE(db.tableExists("documents"));
    EXPECT_TRUE(db.tableExists("pack_fts"));
    EXPECT_TRUE(db.tableExists("bookmarks"));
    EXPECT_EQ(migrate(db), 0);
}

EDEN_TEST(database_binds_and_reads_all_types) {
    Database db = Database::openMemory();
    db.exec("CREATE TABLE t(i INTEGER, r REAL, s TEXT, b BLOB, n TEXT)");
    Statement insert = db.prepare("INSERT INTO t VALUES(:i, :r, :s, :b, :n)");
    insert.bindInt(":i", 42);
    insert.bindDouble(":r", 2.5);
    insert.bindText(":s", "caf\xC3\xA9");
    insert.bindBlob(":b", Bytes{1, 2, 3});
    insert.bindNull(":n");
    insert.execute();
    Statement select = db.prepare("SELECT i, r, s, b, n FROM t");
    EXPECT_TRUE(select.step());
    EXPECT_EQ(select.getInt(0), static_cast<std::int64_t>(42));
    EXPECT_EQ(select.getDouble(1), 2.5);
    EXPECT_EQ(select.getText(2), std::string("caf\xC3\xA9"));
    EXPECT_EQ(select.getBlob(3), (Bytes{1, 2, 3}));
    EXPECT_TRUE(select.isNull(4));
    EXPECT_FALSE(select.step());
}

EDEN_TEST(database_rejects_unknown_parameter) {
    Database db = Database::openMemory();
    db.exec("CREATE TABLE t(a INTEGER)");
    Statement stmt = db.prepare("INSERT INTO t VALUES(:a)");
    bool thrown = false;
    try {
        stmt.bindInt(":missing", 1);
    } catch (const DbError&) {
        thrown = true;
    }
    EXPECT_TRUE(thrown);
}

EDEN_TEST(transaction_rolls_back_unless_committed) {
    Database db = Database::openMemory();
    db.exec("CREATE TABLE t(a INTEGER)");
    {
        Transaction transaction(db);
        db.exec("INSERT INTO t VALUES(1)");
    }
    EXPECT_EQ(db.scalarInt("SELECT COUNT(*) FROM t"), static_cast<std::int64_t>(0));
    {
        Transaction transaction(db);
        db.exec("INSERT INTO t VALUES(1)");
        transaction.commit();
    }
    EXPECT_EQ(db.scalarInt("SELECT COUNT(*) FROM t"), static_cast<std::int64_t>(1));
}

EDEN_TEST(nested_transactions_use_savepoints) {
    Database db = Database::openMemory();
    db.exec("CREATE TABLE t(a INTEGER)");
    Transaction outer(db);
    db.exec("INSERT INTO t VALUES(1)");
    {
        Transaction inner(db);
        db.exec("INSERT INTO t VALUES(2)");
    }
    EXPECT_EQ(db.transactionDepth(), 1);
    outer.commit();
    EXPECT_EQ(db.scalarInt("SELECT COUNT(*) FROM t"), static_cast<std::int64_t>(1));
}

EDEN_TEST(foreign_keys_cascade_on_delete) {
    Database db = Database::openMemory();
    migrate(db);
    db.exec("INSERT INTO users(user_id, display_name, created_at) VALUES('u', 'U', 1)");
    db.exec("INSERT INTO documents(doc_id, title, file_type, owner_id, file_path, content_hash, created_at) "
            "VALUES('d', 'T', 'txt', 'u', '/x', 'h', 1)");
    db.exec("INSERT INTO research_packs(pack_id, doc_id, start_offset, end_offset, text_content, order_index, created_at) "
            "VALUES('p', 'd', 0, 4, 'text', 0, 1)");
    db.exec("DELETE FROM documents WHERE doc_id = 'd'");
    EXPECT_EQ(db.scalarInt("SELECT COUNT(*) FROM research_packs"), static_cast<std::int64_t>(0));
}
