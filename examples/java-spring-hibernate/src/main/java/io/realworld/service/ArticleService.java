package io.realworld.service;

import io.realworld.dto.ArticleDTO;
import io.realworld.dto.ProfileDTO;
import io.realworld.entity.Article;
import io.realworld.entity.User;
import io.realworld.repository.ArticleRepository;
import io.realworld.repository.UserRepository;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.data.domain.Pageable;
import org.springframework.data.domain.Sort;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import java.util.*;
import java.util.stream.Collectors;

@Service
public class ArticleService {
    
    private final ArticleRepository articleRepository;
    private final UserRepository userRepository;
    private final UserService userService;
    
    public ArticleService(ArticleRepository articleRepository, UserRepository userRepository, UserService userService) {
        this.articleRepository = articleRepository;
        this.userRepository = userRepository;
        this.userService = userService;
    }
    
    @Transactional
    public Map<String, Object> createArticle(User author, Map<String, Object> request) {
        Map<String, Object> articleData = (Map<String, Object>) request.get("article");
        
        String title = (String) articleData.get("title");
        String description = (String) articleData.get("description");
        String body = (String) articleData.get("body");
        
        if (title == null || title.isEmpty()) {
            return Map.of("errors", Map.of("body", Arrays.asList("title can't be empty")));
        }
        if (description == null || description.isEmpty()) {
            return Map.of("errors", Map.of("body", Arrays.asList("description can't be empty")));
        }
        if (body == null || body.isEmpty()) {
            return Map.of("errors", Map.of("body", Arrays.asList("body can't be empty")));
        }
        
        // Reload the author to attach to current session
        User managedAuthor = userRepository.findById(author.getId()).orElseThrow();
        
        Article article = new Article();
        article.setTitle(title);
        article.setDescription(description);
        article.setBody(body);
        article.setSlug(generateSlug(title));
        article.setAuthor(managedAuthor);
        
        if (articleData.containsKey("tagList")) {
            article.setTagList((List<String>) articleData.get("tagList"));
        }
        
        articleRepository.save(article);
        
        return Map.of("article", convertToDTO(article, author));
    }
    
    @Transactional(readOnly = true)
    public Map<String, Object> getArticle(String slug, User currentUser) {
        Article article = articleRepository.findBySlug(slug).orElse(null);
        if (article == null) {
            return null;
        }
        return Map.of("article", convertToDTO(article, currentUser));
    }
    
    @Transactional
    public Map<String, Object> updateArticle(String slug, User currentUser, Map<String, Object> request) {
        Article article = articleRepository.findBySlug(slug).orElse(null);
        if (article == null) {
            return null;
        }
        
        if (!article.getAuthor().getId().equals(currentUser.getId())) {
            return Map.of("error", "forbidden");
        }
        
        Map<String, Object> articleData = (Map<String, Object>) request.get("article");
        
        if (articleData.containsKey("title")) {
            String newTitle = (String) articleData.get("title");
            if (newTitle != null && !newTitle.isEmpty()) {
                article.setTitle(newTitle);
                article.setSlug(generateSlug(newTitle));
            }
        }
        if (articleData.containsKey("description")) {
            article.setDescription((String) articleData.get("description"));
        }
        if (articleData.containsKey("body")) {
            article.setBody((String) articleData.get("body"));
        }
        
        // Ensure updatedAt is always after createdAt by adding 1 millisecond if needed
        java.time.LocalDateTime now = java.time.LocalDateTime.now();
        if (!now.isAfter(article.getCreatedAt())) {
            now = article.getCreatedAt().plusNanos(1_000_000); // Add 1 millisecond
        }
        article.setUpdatedAt(now);
        
        articleRepository.save(article);
        
        return Map.of("article", convertToDTO(article, currentUser));
    }
    
    @Transactional
    public Map<String, Object> deleteArticle(String slug, User currentUser) {
        Article article = articleRepository.findBySlug(slug).orElse(null);
        if (article == null) {
            return Map.of("error", "not_found");
        }
        
        if (!article.getAuthor().getId().equals(currentUser.getId())) {
            return Map.of("error", "forbidden");
        }
        
        articleRepository.delete(article);
        return Map.of("success", true);
    }
    
    @Transactional(readOnly = true)
    public Map<String, Object> listArticles(String tag, String author, String favorited, Integer limit, Integer offset, User currentUser) {
        Pageable pageable = PageRequest.of(offset / limit, limit, Sort.by(Sort.Direction.DESC, "createdAt"));
        
        Page<Article> articles;
        
        if (tag != null) {
            articles = articleRepository.findByTag(tag, pageable);
        } else if (author != null) {
            User authorUser = userRepository.findByUsername(author).orElse(null);
            if (authorUser == null) {
                return Map.of("articles", Collections.emptyList(), "articlesCount", 0);
            }
            articles = articleRepository.findByAuthor(authorUser, pageable);
        } else if (favorited != null) {
            articles = articleRepository.findByFavoritedUsername(favorited, pageable);
        } else {
            articles = articleRepository.findAll(pageable);
        }
        
        List<ArticleDTO> articleDTOs = articles.getContent().stream()
                .map(a -> convertToDTO(a, currentUser))
                .collect(Collectors.toList());
        
        return Map.of("articles", articleDTOs, "articlesCount", (int) articles.getTotalElements());
    }
    
    @Transactional(readOnly = true)
    public Map<String, Object> feedArticles(User currentUser, Integer limit, Integer offset) {
        // Reload user to get following list
        User managedUser = userRepository.findById(currentUser.getId()).orElseThrow();
        List<User> following = new ArrayList<>(managedUser.getFollowing());
        if (following.isEmpty()) {
            return Map.of("articles", Collections.emptyList(), "articlesCount", 0);
        }
        
        Pageable pageable = PageRequest.of(offset / limit, limit, Sort.by(Sort.Direction.DESC, "createdAt"));
        Page<Article> articles = articleRepository.findByAuthors(following, pageable);
        
        List<ArticleDTO> articleDTOs = articles.getContent().stream()
                .map(a -> convertToDTO(a, currentUser))
                .collect(Collectors.toList());
        
        return Map.of("articles", articleDTOs, "articlesCount", (int) articles.getTotalElements());
    }
    
    @Transactional
    public Map<String, Object> favoriteArticle(String slug, User currentUser) {
        Article article = articleRepository.findBySlug(slug).orElse(null);
        if (article == null) {
            return null;
        }
        
        // Reload user to attach to current session
        User managedUser = userRepository.findById(currentUser.getId()).orElseThrow();
        managedUser.getFavorites().add(article);
        userRepository.save(managedUser);
        
        return Map.of("article", convertToDTO(article, currentUser));
    }
    
    @Transactional
    public Map<String, Object> unfavoriteArticle(String slug, User currentUser) {
        Article article = articleRepository.findBySlug(slug).orElse(null);
        if (article == null) {
            return null;
        }
        
        // Reload user to attach to current session
        User managedUser = userRepository.findById(currentUser.getId()).orElseThrow();
        managedUser.getFavorites().remove(article);
        userRepository.save(managedUser);
        
        return Map.of("article", convertToDTO(article, currentUser));
    }
    
    private String generateSlug(String title) {
        String slug = title.toLowerCase()
                .replaceAll("[^a-z0-9\\s-]", "")
                .replaceAll("\\s+", "-")
                .replaceAll("-+", "-")
                .replaceAll("^-|-$", "");
        
        String randomStr = UUID.randomUUID().toString().substring(0, 6);
        return slug + "-" + randomStr;
    }
    
    public ArticleDTO convertToDTO(Article article, User currentUser) {
        ArticleDTO dto = new ArticleDTO();
        dto.setSlug(article.getSlug());
        dto.setTitle(article.getTitle());
        dto.setDescription(article.getDescription());
        dto.setBody(article.getBody());
        dto.setTagList(article.getTagList());
        dto.setCreatedAt(article.getCreatedAt());
        dto.setUpdatedAt(article.getUpdatedAt());
        
        boolean favorited = false;
        if (currentUser != null) {
            favorited = userRepository.hasFavorited(currentUser.getId(), article.getId());
        }
        dto.setFavorited(favorited);
        dto.setFavoritesCount(article.getFavoritedBy().size());
        dto.setAuthor(userService.convertToProfileDTO(article.getAuthor(), currentUser));
        return dto;
    }
}
