package io.realworld.repository;

import io.realworld.entity.User;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import java.util.Optional;

public interface UserRepository extends JpaRepository<User, Long> {
    Optional<User> findByEmail(String email);
    Optional<User> findByUsername(String username);
    boolean existsByEmail(String email);
    boolean existsByUsername(String username);
    
    @Query("SELECT DISTINCT u FROM User u LEFT JOIN FETCH u.favorites WHERE u.id = :id")
    Optional<User> findByIdWithFavorites(@Param("id") Long id);
    
    @Query("SELECT DISTINCT u FROM User u LEFT JOIN FETCH u.following WHERE u.id = :id")
    Optional<User> findByIdWithFollowing(@Param("id") Long id);
    
    @Query("SELECT DISTINCT u FROM User u LEFT JOIN FETCH u.followers WHERE u.id = :id")
    Optional<User> findByIdWithFollowers(@Param("id") Long id);
    
    @Query("SELECT CASE WHEN COUNT(f) > 0 THEN true ELSE false END FROM User u JOIN u.followers f WHERE u.id = :userId AND f.id = :followerId")
    boolean isFollowedBy(@Param("userId") Long userId, @Param("followerId") Long followerId);
    
    @Query("SELECT CASE WHEN COUNT(a) > 0 THEN true ELSE false END FROM User u JOIN u.favorites a WHERE u.id = :userId AND a.id = :articleId")
    boolean hasFavorited(@Param("userId") Long userId, @Param("articleId") Long articleId);
}
