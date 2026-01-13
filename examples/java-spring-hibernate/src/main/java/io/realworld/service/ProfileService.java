package io.realworld.service;

import io.realworld.entity.User;
import io.realworld.repository.UserRepository;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import java.util.Map;

@Service
public class ProfileService {
    
    private final UserRepository userRepository;
    private final UserService userService;
    
    public ProfileService(UserRepository userRepository, UserService userService) {
        this.userRepository = userRepository;
        this.userService = userService;
    }
    
    @Transactional(readOnly = true)
    public Map<String, Object> getProfile(String username, User currentUser) {
        User user = userRepository.findByUsername(username).orElse(null);
        if (user == null) {
            return null;
        }
        return Map.of("profile", userService.convertToProfileDTO(user, currentUser));
    }
    
    @Transactional
    public Map<String, Object> followUser(String username, User currentUser) {
        User userToFollow = userRepository.findByUsername(username).orElse(null);
        if (userToFollow == null) {
            return null;
        }
        
        // Reload user to attach to current session
        User managedUser = userRepository.findById(currentUser.getId()).orElseThrow();
        managedUser.getFollowing().add(userToFollow);
        userRepository.save(managedUser);
        
        return Map.of("profile", userService.convertToProfileDTO(userToFollow, currentUser));
    }
    
    @Transactional
    public Map<String, Object> unfollowUser(String username, User currentUser) {
        User userToUnfollow = userRepository.findByUsername(username).orElse(null);
        if (userToUnfollow == null) {
            return null;
        }
        
        // Reload user to attach to current session
        User managedUser = userRepository.findById(currentUser.getId()).orElseThrow();
        managedUser.getFollowing().remove(userToUnfollow);
        userRepository.save(managedUser);
        
        return Map.of("profile", userService.convertToProfileDTO(userToUnfollow, currentUser));
    }
}
