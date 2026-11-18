# syntax=docker/dockerfile:1
# PolyExecution Studio - Web Client Dockerfile
# Multi-stage build producing static assets served by Nginx

# Stage 1: Build static React SPA
FROM node:22-alpine AS builder
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci
COPY . ./
RUN npm run build

# Stage 2: Minimal Nginx web server
FROM nginx:alpine
COPY --from=builder /app/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80

CMD ["nginx", "-g", "daemon off;"]
