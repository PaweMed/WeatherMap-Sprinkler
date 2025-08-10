const CACHE_NAME = 'sprinkler-app-v1';
const urlsToCache = [
  '/',
  '/src/main.tsx',
  '/src/index.css',
  '/manifest.json',
  // Add other critical assets
];

// Install event
self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME)
      .then((cache) => {
        return cache.addAll(urlsToCache);
      })
  );
});

// Fetch event
self.addEventListener('fetch', (event) => {
  event.respondWith(
    caches.match(event.request)
      .then((response) => {
        // Return cached version or fetch from network
        if (response) {
          return response;
        }
        
        // Clone the request
        const fetchRequest = event.request.clone();
        
        return fetch(fetchRequest).then((response) => {
          // Check if valid response
          if (!response || response.status !== 200 || response.type !== 'basic') {
            return response;
          }
          
          // Clone the response
          const responseToCache = response.clone();
          
          caches.open(CACHE_NAME)
            .then((cache) => {
              cache.put(event.request, responseToCache);
            });
          
          return response;
        }).catch(() => {
          // Return offline page or cached content for navigation requests
          if (event.request.destination === 'document') {
            return caches.match('/');
          }
        });
      })
  );
});

// Activate event
self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((cacheNames) => {
      return Promise.all(
        cacheNames.map((cacheName) => {
          if (cacheName !== CACHE_NAME) {
            return caches.delete(cacheName);
          }
        })
      );
    })
  );
});

// Background sync for offline operations
self.addEventListener('sync', (event) => {
  if (event.tag === 'zone-control') {
    event.waitUntil(syncZoneOperations());
  }
});

async function syncZoneOperations() {
  // Get pending operations from IndexedDB and sync with server
  try {
    const pendingOperations = await getPendingOperations();
    for (const operation of pendingOperations) {
      await fetch('/api' + operation.endpoint, {
        method: operation.method,
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(operation.data)
      });
      await removePendingOperation(operation.id);
    }
  } catch (error) {
    console.error('Background sync failed:', error);
  }
}

// Mock functions for offline storage (would use IndexedDB in production)
async function getPendingOperations() {
  return [];
}

async function removePendingOperation(id) {
  // Remove from IndexedDB
}

// Push notifications for zone status
self.addEventListener('push', (event) => {
  const options = {
    body: event.data ? event.data.text() : 'Sprawdź status nawadniania',
    icon: '/manifest-icon-192.png',
    badge: '/manifest-icon-96.png',
    tag: 'sprinkler-notification',
    requireInteraction: true,
    actions: [
      {
        action: 'open-zones',
        title: 'Otwórz strefy'
      },
      {
        action: 'dismiss',
        title: 'Zamknij'
      }
    ]
  };
  
  event.waitUntil(
    self.registration.showNotification('System Nawadniania', options)
  );
});

// Handle notification clicks
self.addEventListener('notificationclick', (event) => {
  event.notification.close();
  
  if (event.action === 'open-zones') {
    event.waitUntil(
      clients.openWindow('/?section=zones')
    );
  } else {
    event.waitUntil(
      clients.openWindow('/')
    );
  }
});
