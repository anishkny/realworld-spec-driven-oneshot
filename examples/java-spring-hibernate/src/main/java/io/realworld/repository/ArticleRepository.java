package io.realworld.repository;

import io.realworld.entity.Article;
import io.realworld.entity.User;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import java.util.List;
import java.util.Optional;

public interface ArticleRepository extends JpaRepository<Article, Long> {
    Optional<Article> findBySlug(String slug);
    
    @Query("SELECT DISTINCT a FROM Article a LEFT JOIN FETCH a.favoritedBy LEFT JOIN FETCH a.author WHERE a.slug = :slug")
    Optional<Article> findBySlugWithAssociations(@Param("slug") String slug);
    
    @Query("SELECT a FROM Article a WHERE :tag MEMBER OF a.tagList ORDER BY a.createdAt DESC")
    Page<Article> findByTag(@Param("tag") String tag, Pageable pageable);
    
    Page<Article> findByAuthor(User author, Pageable pageable);
    
    @Query("SELECT a FROM Article a JOIN a.favoritedBy f WHERE f.username = :username ORDER BY a.createdAt DESC")
    Page<Article> findByFavoritedUsername(@Param("username") String username, Pageable pageable);
    
    @Query("SELECT a FROM Article a WHERE a.author IN :authors ORDER BY a.createdAt DESC")
    Page<Article> findByAuthors(@Param("authors") List<User> authors, Pageable pageable);
    
    @Query("SELECT DISTINCT t FROM Article a JOIN a.tagList t")
    List<String> findAllTags();
}
