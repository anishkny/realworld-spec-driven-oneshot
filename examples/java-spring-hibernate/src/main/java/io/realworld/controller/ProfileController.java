package io.realworld.controller;

import io.realworld.entity.User;
import io.realworld.service.ProfileService;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.Authentication;
import org.springframework.web.bind.annotation.*;
import java.util.Map;

@RestController
@RequestMapping("/api/profiles")
public class ProfileController {
    
    private final ProfileService profileService;
    
    public ProfileController(ProfileService profileService) {
        this.profileService = profileService;
    }
    
    @GetMapping("/{username}")
    public ResponseEntity<Map<String, Object>> getProfile(@PathVariable String username, Authentication authentication) {
        User currentUser = authentication != null && authentication.getPrincipal() instanceof User 
                ? (User) authentication.getPrincipal() : null;
        
        Map<String, Object> response = profileService.getProfile(username, currentUser);
        if (response == null) {
            return ResponseEntity.status(404).build();
        }
        return ResponseEntity.ok(response);
    }
    
    @PostMapping("/{username}/follow")
    public ResponseEntity<Map<String, Object>> followUser(@PathVariable String username, Authentication authentication) {
        if (authentication == null || !(authentication.getPrincipal() instanceof User)) {
            return ResponseEntity.status(401).build();
        }
        
        User currentUser = (User) authentication.getPrincipal();
        Map<String, Object> response = profileService.followUser(username, currentUser);
        
        if (response == null) {
            return ResponseEntity.status(404).build();
        }
        return ResponseEntity.ok(response);
    }
    
    @DeleteMapping("/{username}/follow")
    public ResponseEntity<Map<String, Object>> unfollowUser(@PathVariable String username, Authentication authentication) {
        if (authentication == null || !(authentication.getPrincipal() instanceof User)) {
            return ResponseEntity.status(401).build();
        }
        
        User currentUser = (User) authentication.getPrincipal();
        Map<String, Object> response = profileService.unfollowUser(username, currentUser);
        
        if (response == null) {
            return ResponseEntity.status(404).build();
        }
        return ResponseEntity.ok(response);
    }
}
