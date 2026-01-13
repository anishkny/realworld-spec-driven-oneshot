package io.realworld.service;

import io.realworld.dto.CommentDTO;
import io.realworld.entity.Article;
import io.realworld.entity.Comment;
import io.realworld.entity.User;
import io.realworld.repository.ArticleRepository;
import io.realworld.repository.CommentRepository;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import java.util.*;
import java.util.stream.Collectors;

@Service
public class CommentService {
    
    private final CommentRepository commentRepository;
    private final ArticleRepository articleRepository;
    private final UserService userService;
    
    public CommentService(CommentRepository commentRepository, ArticleRepository articleRepository, UserService userService) {
        this.commentRepository = commentRepository;
        this.articleRepository = articleRepository;
        this.userService = userService;
    }
    
    @Transactional
    public Map<String, Object> addComment(String slug, User author, Map<String, Object> request) {
        Article article = articleRepository.findBySlug(slug).orElse(null);
        if (article == null) {
            return null;
        }
        
        Map<String, Object> commentData = (Map<String, Object>) request.get("comment");
        String body = (String) commentData.get("body");
        
        if (body == null || body.isEmpty()) {
            return Map.of("errors", Map.of("body", Arrays.asList("comment body can't be empty")));
        }
        
        Comment comment = new Comment();
        comment.setBody(body);
        comment.setAuthor(author);
        comment.setArticle(article);
        
        commentRepository.save(comment);
        
        return Map.of("comment", convertToDTO(comment, author));
    }
    
    @Transactional(readOnly = true)
    public Map<String, Object> getComments(String slug, User currentUser) {
        Article article = articleRepository.findBySlug(slug).orElse(null);
        if (article == null) {
            return null;
        }
        
        List<Comment> comments = commentRepository.findByArticleOrderByCreatedAtDesc(article);
        List<CommentDTO> commentDTOs = comments.stream()
                .map(c -> convertToDTO(c, currentUser))
                .collect(Collectors.toList());
        
        return Map.of("comments", commentDTOs);
    }
    
    @Transactional
    public Map<String, Object> deleteComment(String slug, Long commentId, User currentUser) {
        Article article = articleRepository.findBySlug(slug).orElse(null);
        if (article == null) {
            return Map.of("error", "article_not_found");
        }
        
        Comment comment = commentRepository.findById(commentId).orElse(null);
        if (comment == null) {
            return Map.of("error", "comment_not_found");
        }
        
        if (!comment.getAuthor().getId().equals(currentUser.getId())) {
            return Map.of("error", "forbidden");
        }
        
        commentRepository.delete(comment);
        return Map.of("success", true);
    }
    
    public CommentDTO convertToDTO(Comment comment, User currentUser) {
        CommentDTO dto = new CommentDTO();
        dto.setId(comment.getId());
        dto.setBody(comment.getBody());
        dto.setCreatedAt(comment.getCreatedAt());
        dto.setUpdatedAt(comment.getUpdatedAt());
        dto.setAuthor(userService.convertToProfileDTO(comment.getAuthor(), currentUser));
        return dto;
    }
}
