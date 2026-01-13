package io.realworld.config;

import org.springframework.boot.jdbc.DataSourceBuilder;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import javax.sql.DataSource;

@Configuration
public class DataSourceConfig {
    
    @Bean
    public DataSource dataSource() {
        String postgresUri = System.getenv("POSTGRES_URI");
        
        String url;
        String username = "postgres";
        String password = "password";
        
        if (postgresUri != null && postgresUri.startsWith("postgres://")) {
            url = postgresUri.replace("postgres://", "jdbc:postgresql://");
            
            // Parse username and password if embedded in URI
            if (url.contains("@")) {
                String[] parts = url.split("@", 2);
                String credentials = parts[0].replace("jdbc:postgresql://", "");
                url = "jdbc:postgresql://" + parts[1];
                
                if (credentials.contains(":")) {
                    String[] creds = credentials.split(":", 2);
                    username = creds[0];
                    password = creds[1];
                }
            }
        } else if (postgresUri != null && postgresUri.startsWith("jdbc:")) {
            url = postgresUri;
        } else {
            url = "jdbc:postgresql://localhost:5432/postgres";
        }
        
        return DataSourceBuilder.create()
                .url(url)
                .username(username)
                .password(password)
                .build();
    }
}
