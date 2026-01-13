package io.realworld.controller;

import io.realworld.repository.ArticleRepository;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/tags")
public class TagController {
    
    private final ArticleRepository articleRepository;
    
    public TagController(ArticleRepository articleRepository) {
        this.articleRepository = articleRepository;
    }
    
    @GetMapping
    public ResponseEntity<Map<String, List<String>>> getTags() {
        List<String> tags = articleRepository.findAllTags();
        return ResponseEntity.ok(Map.of("tags", tags));
    }
}
