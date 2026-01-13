package io.realworld.repository;

import io.realworld.entity.Article;
import io.realworld.entity.Comment;
import org.springframework.data.jpa.repository.JpaRepository;
import java.util.List;

public interface CommentRepository extends JpaRepository<Comment, Long> {
    List<Comment> findByArticleOrderByCreatedAtDesc(Article article);
}
