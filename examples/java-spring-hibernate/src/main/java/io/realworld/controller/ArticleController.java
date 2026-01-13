package io.realworld.controller;

import io.realworld.entity.User;
import io.realworld.service.ArticleService;
import io.realworld.service.CommentService;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.Authentication;
import org.springframework.web.bind.annotation.*;
import java.util.Map;

@RestController
@RequestMapping("/api/articles")
public class ArticleController {
    
    private final ArticleService articleService;
    private final CommentService commentService;
    
    public ArticleController(ArticleService articleService, CommentService commentService) {
        this.articleService = articleService;
        this.commentService = commentService;
    }
    
    @PostMapping
    public ResponseEntity<Map<String, Object>> createArticle(Authentication authentication, @RequestBody Map<String, Object> request) {
        if (authentication == null || !(authentication.getPrincipal() instanceof User)) {
            return ResponseEntity.status(401).build();
        }
        
        User currentUser = (User) authentication.getPrincipal();
        Map<String, Object> response = articleService.createArticle(currentUser, request);
        
        if (response.containsKey("errors")) {
            return ResponseEntity.status(422).body(response);
        }
        
        return ResponseEntity.ok(response);
    }
    
    @GetMapping("/{slug}")
    public ResponseEntity<Map<String, Object>> getArticle(@PathVariable String slug, Authentication authentication) {
        User currentUser = authentication != null && authentication.getPrincipal() instanceof User 
                ? (User) authentication.getPrincipal() : null;
        
        Map<String, Object> response = articleService.getArticle(slug, currentUser);
        if (response == null) {
            return ResponseEntity.status(404).build();
        }
        return ResponseEntity.ok(response);
    }
    
    @PutMapping("/{slug}")
    public ResponseEntity<Map<String, Object>> updateArticle(@PathVariable String slug, Authentication authentication, @RequestBody Map<String, Object> request) {
        if (authentication == null || !(authentication.getPrincipal() instanceof User)) {
            return ResponseEntity.status(401).build();
        }
        
        User currentUser = (User) authentication.getPrincipal();
        Map<String, Object> response = articleService.updateArticle(slug, currentUser, request);
        
        if (response == null) {
            return ResponseEntity.status(404).build();
        }
        if (response.containsKey("error") && response.get("error").equals("forbidden")) {
            return ResponseEntity.status(403).build();
        }
        
        return ResponseEntity.ok(response);
    }
    
    @DeleteMapping("/{slug}")
    public ResponseEntity<Void> deleteArticle(@PathVariable String slug, Authentication authentication) {
        if (authentication == null || !(authentication.getPrincipal() instanceof User)) {
            return ResponseEntity.status(401).build();
        }
        
        User currentUser = (User) authentication.getPrincipal();
        Map<String, Object> result = articleService.deleteArticle(slug, currentUser);
        
        if (result.containsKey("error")) {
            String error = (String) result.get("error");
            if (error.equals("not_found")) {
                return ResponseEntity.status(404).build();
            }
            if (error.equals("forbidden")) {
                return ResponseEntity.status(403).build();
            }
        }
        
        return ResponseEntity.ok().build();
    }
    
    @GetMapping
    public ResponseEntity<Map<String, Object>> listArticles(
            @RequestParam(required = false) String tag,
            @RequestParam(required = false) String author,
            @RequestParam(required = false) String favorited,
            @RequestParam(defaultValue = "20") Integer limit,
            @RequestParam(defaultValue = "0") Integer offset,
            Authentication authentication) {
        
        User currentUser = authentication != null && authentication.getPrincipal() instanceof User 
                ? (User) authentication.getPrincipal() : null;
        
        Map<String, Object> response = articleService.listArticles(tag, author, favorited, limit, offset, currentUser);
        return ResponseEntity.ok(response);
    }
    
    @GetMapping("/feed")
    public ResponseEntity<Map<String, Object>> feedArticles(
            @RequestParam(defaultValue = "20") Integer limit,
            @RequestParam(defaultValue = "0") Integer offset,
            Authentication authentication) {
        
        if (authentication == null || !(authentication.getPrincipal() instanceof User)) {
            return ResponseEntity.status(401).build();
        }
        
        User currentUser = (User) authentication.getPrincipal();
        Map<String, Object> response = articleService.feedArticles(currentUser, limit, offset);
        return ResponseEntity.ok(response);
    }
    
    @PostMapping("/{slug}/favorite")
    public ResponseEntity<Map<String, Object>> favoriteArticle(@PathVariable String slug, Authentication authentication) {
        if (authentication == null || !(authentication.getPrincipal() instanceof User)) {
            return ResponseEntity.status(401).build();
        }
        
        User currentUser = (User) authentication.getPrincipal();
        Map<String, Object> response = articleService.favoriteArticle(slug, currentUser);
        
        if (response == null) {
            return ResponseEntity.status(404).build();
        }
        
        return ResponseEntity.ok(response);
    }
    
    @DeleteMapping("/{slug}/favorite")
    public ResponseEntity<Map<String, Object>> unfavoriteArticle(@PathVariable String slug, Authentication authentication) {
        if (authentication == null || !(authentication.getPrincipal() instanceof User)) {
            return ResponseEntity.status(401).build();
        }
        
        User currentUser = (User) authentication.getPrincipal();
        Map<String, Object> response = articleService.unfavoriteArticle(slug, currentUser);
        
        if (response == null) {
            return ResponseEntity.status(404).build();
        }
        
        return ResponseEntity.ok(response);
    }
    
    @PostMapping("/{slug}/comments")
    public ResponseEntity<Map<String, Object>> addComment(@PathVariable String slug, Authentication authentication, @RequestBody Map<String, Object> request) {
        if (authentication == null || !(authentication.getPrincipal() instanceof User)) {
            return ResponseEntity.status(401).build();
        }
        
        User currentUser = (User) authentication.getPrincipal();
        Map<String, Object> response = commentService.addComment(slug, currentUser, request);
        
        if (response == null) {
            return ResponseEntity.status(404).build();
        }
        if (response.containsKey("errors")) {
            return ResponseEntity.status(422).body(response);
        }
        
        return ResponseEntity.ok(response);
    }
    
    @GetMapping("/{slug}/comments")
    public ResponseEntity<Map<String, Object>> getComments(@PathVariable String slug, Authentication authentication) {
        User currentUser = authentication != null && authentication.getPrincipal() instanceof User 
                ? (User) authentication.getPrincipal() : null;
        
        Map<String, Object> response = commentService.getComments(slug, currentUser);
        
        if (response == null) {
            return ResponseEntity.status(404).build();
        }
        
        return ResponseEntity.ok(response);
    }
    
    @DeleteMapping("/{slug}/comments/{id}")
    public ResponseEntity<Void> deleteComment(@PathVariable String slug, @PathVariable Long id, Authentication authentication) {
        if (authentication == null || !(authentication.getPrincipal() instanceof User)) {
            return ResponseEntity.status(401).build();
        }
        
        User currentUser = (User) authentication.getPrincipal();
        Map<String, Object> response = commentService.deleteComment(slug, id, currentUser);
        
        if (response.containsKey("error")) {
            String error = (String) response.get("error");
            if (error.equals("article_not_found") || error.equals("comment_not_found")) {
                return ResponseEntity.status(404).build();
            }
            if (error.equals("forbidden")) {
                return ResponseEntity.status(403).build();
            }
        }
        
        return ResponseEntity.ok().build();
    }
}
