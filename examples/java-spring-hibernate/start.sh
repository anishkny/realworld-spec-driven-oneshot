#!/bin/bash
cd "$(dirname "$0")"
mvn clean package -DskipTests
java -jar target/realworld-api-1.0.0.jar
