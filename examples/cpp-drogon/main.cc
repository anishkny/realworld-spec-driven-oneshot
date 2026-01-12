#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <iostream>
#include <regex>

using namespace drogon;

void initDatabase() {
    try {
        auto dbClient = app().getDbClient("default");
        
        // Drop existing tables
        dbClient->execSqlSync("DROP TABLE IF EXISTS favorites CASCADE;");
        dbClient->execSqlSync("DROP TABLE IF EXISTS comments CASCADE;");
        dbClient->execSqlSync("DROP TABLE IF EXISTS article_tags CASCADE;");
        dbClient->execSqlSync("DROP TABLE IF EXISTS articles CASCADE;");
        dbClient->execSqlSync("DROP TABLE IF EXISTS followers CASCADE;");
        dbClient->execSqlSync("DROP TABLE IF EXISTS users CASCADE;");
        
        // Create users table
        dbClient->execSqlSync(R"(
            CREATE TABLE users (
                id SERIAL PRIMARY KEY,
                username VARCHAR(255) UNIQUE NOT NULL,
                email VARCHAR(255) UNIQUE NOT NULL,
                password VARCHAR(255) NOT NULL,
                bio TEXT,
                image TEXT
            );
        )");
        
        // Create followers table
        dbClient->execSqlSync(R"(
            CREATE TABLE followers (
                follower_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
                followed_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
                PRIMARY KEY (follower_id, followed_id)
            );
        )");
        
        // Create articles table
        dbClient->execSqlSync(R"(
            CREATE TABLE articles (
                id SERIAL PRIMARY KEY,
                slug VARCHAR(255) UNIQUE NOT NULL,
                title VARCHAR(255) NOT NULL,
                description TEXT NOT NULL,
                body TEXT NOT NULL,
                author_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            );
        )");
        
        // Create article_tags table
        dbClient->execSqlSync(R"(
            CREATE TABLE article_tags (
                article_id INTEGER REFERENCES articles(id) ON DELETE CASCADE,
                tag VARCHAR(255) NOT NULL,
                PRIMARY KEY (article_id, tag)
            );
        )");
        
        // Create comments table
        dbClient->execSqlSync(R"(
            CREATE TABLE comments (
                id SERIAL PRIMARY KEY,
                body TEXT NOT NULL,
                article_id INTEGER REFERENCES articles(id) ON DELETE CASCADE,
                author_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            );
        )");
        
        // Create favorites table
        dbClient->execSqlSync(R"(
            CREATE TABLE favorites (
                user_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
                article_id INTEGER REFERENCES articles(id) ON DELETE CASCADE,
                PRIMARY KEY (user_id, article_id)
            );
        )");
        
        std::cout << "Database initialized successfully" << std::endl;
    } catch (const orm::DrogonDbException &e) {
        std::cerr << "Database initialization error: " << e.base().what() << std::endl;
        exit(1);
    }
}

int main() {
    const char* postgres_uri = std::getenv("POSTGRES_URI");
    if (!postgres_uri) {
        postgres_uri = "postgres://postgres:password@localhost:5432/postgres";
    }

    // Parse PostgreSQL URI: postgres://user:password@host:port/dbname
    std::string uri_str(postgres_uri);
    std::regex uri_regex("postgres://([^:]+):([^@]+)@([^:]+):(\\d+)/(.+)");
    std::smatch matches;
    
    std::string user = "postgres";
    std::string password = "password";
    std::string host = "localhost";
    unsigned short port = 5432;
    std::string dbname = "postgres";
    
    if (std::regex_search(uri_str, matches, uri_regex)) {
        user = matches[1].str();
        password = matches[2].str();
        host = matches[3].str();
        port = std::stoi(matches[4].str());
        dbname = matches[5].str();
    }
    
    app().createDbClient("postgresql", host, port, dbname, user, password, 10, "", "default");

    // Initialize database after event loop starts
    app().registerBeginningAdvice([]() {
        initDatabase();
    });

    app().setLogLevel(trantor::Logger::kWarn);
    app().addListener("0.0.0.0", 3000);
    app().run();
    
    return 0;
}
